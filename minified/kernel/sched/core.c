
#include <linux/sched/signal.h>

#include <linux/sched/clock.h>

/* linux/sched/signal.h already included above */
#include <linux/sched/debug.h>

extern void sched_init(void);
#include <linux/sched/mm.h>

#include <asm/mmu_context.h>

#include <asm/switch_to.h>

#include "sched.h"

DEFINE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);

void raw_spin_rq_lock_nested(struct rq *rq, int subclass)
{
	preempt_disable();
	raw_spin_lock_nested(&rq->__lock, subclass);
	preempt_enable_no_resched();
}

void raw_spin_rq_unlock(struct rq *rq)
{
	raw_spin_unlock(rq_lockp(rq));
}

static struct rq *__task_rq_lock(struct task_struct *p, struct rq_flags *rf)
	__acquires(rq->lock)
{
	struct rq *rq;

	rq = task_rq(p);
	raw_spin_rq_lock(rq);
	rq_pin_lock(rq, rf);
	return rq;
}

void update_rq_clock(struct rq *rq)
{
	s64 delta;

	if (rq->clock_update_flags & RQCF_ACT_SKIP)
		return;

	delta = sched_clock_cpu(0) - rq->clock;
	if (delta < 0)
		return;
	rq->clock += delta;
	rq->clock_task += delta;
}

void resched_curr(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	if (test_tsk_need_resched(curr))
		return;

	set_tsk_thread_flag(
		curr, TIF_NEED_RESCHED); /* set_tsk_need_resched inlined */
	set_preempt_need_resched();
}

void resched_cpu(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	raw_spin_rq_lock_irqsave(rq, flags);
	resched_curr(rq);
	raw_spin_rq_unlock_irqrestore(rq, flags);
}

static void set_load_weight(struct task_struct *p, bool update_load)
{
	int prio = p->static_prio - MAX_RT_PRIO;
	struct load_weight *load = &p->se.load;

	if (update_load && p->sched_class == &fair_sched_class)
		reweight_task(p, prio);
	else
		load->weight = scale_load(sched_prio_to_weight[prio]);
}

static inline void enqueue_task(struct rq *rq, struct task_struct *p, int flags)
{
	if (!(flags & ENQUEUE_NOCLOCK))
		update_rq_clock(rq);

	p->sched_class->enqueue_task(rq, p, flags);
}

static inline void dequeue_task(struct rq *rq, struct task_struct *p, int flags)
{
	if (!(flags & DEQUEUE_NOCLOCK))
		update_rq_clock(rq);

	p->sched_class->dequeue_task(rq, p, flags);
}

static void activate_task(struct rq *rq, struct task_struct *p, int flags)
{
	enqueue_task(rq, p, flags);

	p->on_rq = TASK_ON_RQ_QUEUED;
}

void check_preempt_curr(struct rq *rq, struct task_struct *p, int flags)
{
	if (p->sched_class == rq->curr->sched_class)
		rq->curr->sched_class->check_preempt_curr(rq, p, flags);
	else if (sched_class_above(p->sched_class, rq->curr->sched_class))
		resched_curr(rq);

	if (task_on_rq_queued(rq->curr) && test_tsk_need_resched(rq->curr))
		rq_clock_skip_update(rq);
}

static void ttwu_do_wakeup(struct rq *rq, struct task_struct *p, int wake_flags,
			   struct rq_flags *rf)
{
	check_preempt_curr(rq, p, wake_flags);
	WRITE_ONCE(p->__state, TASK_RUNNING);
}

static int try_to_wake_up(struct task_struct *p, unsigned int state,
			  int wake_flags)
{
	int cpu, success = 0;
	struct rq_flags rf;
	struct rq *rq;

	preempt_disable();
	if (p == current) {
		if (READ_ONCE(p->__state) & state) {
			success = 1;
			WRITE_ONCE(p->__state, TASK_RUNNING);
		}
		goto out;
	}

	if (READ_ONCE(p->__state) == TASK_RUNNING)
		goto out;

	smp_rmb();
	if (READ_ONCE(p->on_rq)) {
		rq = __task_rq_lock(p, &rf);
		if (task_on_rq_queued(p)) {
			update_rq_clock(rq);
			ttwu_do_wakeup(rq, p, wake_flags, &rf);
			__task_rq_unlock(rq, &rf);
			goto out;
		}
		__task_rq_unlock(rq, &rf);
	}

	cpu = task_cpu(p);

	{
		struct rq *rq = cpu_rq(cpu);
		rq_lock(rq, &rf);
		update_rq_clock(rq);
		activate_task(rq, p, ENQUEUE_WAKEUP | ENQUEUE_NOCLOCK);
		ttwu_do_wakeup(rq, p, wake_flags, &rf);
		rq_unlock(rq, &rf);
	}
out:
	preempt_enable();

	return success;
}

int wake_up_process(struct task_struct *p)
{
	return try_to_wake_up(p, TASK_NORMAL, 0);
}

int wake_up_state(struct task_struct *p, unsigned int state)
{
	return try_to_wake_up(p, state, 0);
}

static void __sched_fork(unsigned long clone_flags, struct task_struct *p)
{
	p->on_rq = 0;

	p->se.on_rq = 0;
	p->se.exec_start = 0;
	p->se.sum_exec_runtime = 0;
	p->se.vruntime = 0;
}

int sched_fork(unsigned long clone_flags, struct task_struct *p)
{
	__sched_fork(clone_flags, p);

	p->__state = TASK_NEW;
	p->prio = current->normal_prio;

	p->sched_class = &fair_sched_class;

	return 0;
}

void sched_cgroup_fork(struct task_struct *p, struct kernel_clone_args *kargs)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&p->pi_lock, flags);

	__set_task_cpu(p, smp_processor_id());
	if (p->sched_class->task_fork)
		p->sched_class->task_fork(p);
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);
}

void wake_up_new_task(struct task_struct *p)
{
	struct rq_flags rf;
	struct rq *rq;

	raw_spin_lock_irqsave(&p->pi_lock, rf.flags);
	WRITE_ONCE(p->__state, TASK_RUNNING);
	rq = __task_rq_lock(p, &rf);
	update_rq_clock(rq);

	activate_task(rq, p, ENQUEUE_NOCLOCK);

	check_preempt_curr(rq, p, WF_FORK);
	task_rq_unlock(rq, p, &rf);
}

static struct rq *finish_task_switch(struct task_struct *prev)
	__releases(rq->lock)
{
	struct rq *rq = this_rq();
	struct mm_struct *mm = rq->prev_mm;
	unsigned int prev_state;

	if (WARN_ONCE(preempt_count() != 2 * PREEMPT_DISABLE_OFFSET,
		      "corrupted preempt_count: %s/%d/0x%x\n", current->comm,
		      current->pid, preempt_count()))
		preempt_count_set(FORK_PREEMPT_COUNT);

	rq->prev_mm = NULL;

	prev_state = READ_ONCE(prev->__state);

	raw_spin_rq_unlock_irq(rq);

	if (mm)
		mmdrop_sched(mm);
	if (unlikely(prev_state == TASK_DEAD)) {
		put_task_stack(prev);

		put_task_struct_rcu_user(prev);
	}

	return rq;
}

asmlinkage __visible void schedule_tail(struct task_struct *prev)
	__releases(rq->lock)
{
	finish_task_switch(prev);
	preempt_enable();
}

static __always_inline struct rq *context_switch(struct rq *rq,
						 struct task_struct *prev,
						 struct task_struct *next,
						 struct rq_flags *rf)
{
	if (!next->mm) {
		enter_lazy_tlb(prev->active_mm, next);

		next->active_mm = prev->active_mm;
		if (prev->mm)
			mmgrab(prev->active_mm);
		else
			prev->active_mm = NULL;
	} else {
		switch_mm_irqs_off(prev->active_mm, next->mm, next);

		if (!prev->mm) {
			rq->prev_mm = prev->active_mm;
			prev->active_mm = NULL;
		}
	}

	rq->clock_update_flags &= ~(RQCF_ACT_SKIP | RQCF_REQ_SKIP);

	rq_unpin_lock(rq, rf);

	switch_to(prev, next, prev);
	barrier();

	return finish_task_switch(prev);
}

static inline struct task_struct *
__pick_next_task(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
	struct task_struct *p;

	if (likely(!sched_class_above(prev->sched_class, &fair_sched_class) &&
		   rq->nr_running == rq->cfs.h_nr_running)) {
		p = pick_next_task_fair(rq, prev, rf);

		if (!p) {
			put_prev_task(rq, prev);
			p = pick_next_task_idle(rq);
		}

		return p;
	}

	BUG();
}

#define SM_NONE 0x0
#define SM_PREEMPT 0x1

#define SM_MASK_PREEMPT (~0U)

static void __sched notrace __schedule(unsigned int sched_mode)
{
	struct task_struct *prev, *next;
	unsigned long prev_state;
	struct rq_flags rf;
	struct rq *rq;
	int cpu;

	cpu = smp_processor_id();
	rq = cpu_rq(cpu);
	prev = rq->curr;

	if (unlikely(in_atomic_preempt_off()))
		preempt_count_set(PREEMPT_DISABLED);
	local_irq_disable();
	rcu_note_context_switch(!!sched_mode);

	rq_lock(rq, &rf);
	rq->clock_update_flags <<= 1;
	update_rq_clock(rq);

	prev_state = READ_ONCE(prev->__state);
	if (!(sched_mode & SM_MASK_PREEMPT) && prev_state) {
		if (signal_pending_state(prev_state, prev)) {
			WRITE_ONCE(prev->__state, TASK_RUNNING);
		} else {
			prev->on_rq = 0;
			dequeue_task(rq, prev, DEQUEUE_SLEEP | DEQUEUE_NOCLOCK);
		}
	}

	next = __pick_next_task(rq, prev, &rf);
	clear_tsk_need_resched(prev);
	clear_preempt_need_resched();

	if (likely(prev != next)) {
		RCU_INIT_POINTER(rq->curr, next);

		rq = context_switch(rq, prev, next, &rf);
	} else {
		rq->clock_update_flags &= ~(RQCF_ACT_SKIP | RQCF_REQ_SKIP);

		rq_unpin_lock(rq, &rf);
		raw_spin_rq_unlock_irq(rq);
	}
}

void __noreturn do_task_dead(void)
{
	set_special_state(TASK_DEAD);

	current->flags |= PF_NOFREEZE;

	__schedule(SM_NONE);
	BUG();

	for (;;)
		cpu_relax();
}

asmlinkage __visible void __sched schedule(void)
{
	do {
		preempt_disable();
		__schedule(SM_NONE);
		sched_preempt_enable_no_resched();
	} while (need_resched());
}

void __sched schedule_idle(void)
{
	WARN_ON_ONCE(current->__state);
	do {
		__schedule(SM_NONE);
	} while (need_resched());
}

void __sched schedule_preempt_disabled(void)
{
	sched_preempt_enable_no_resched();
	schedule();
	preempt_disable();
}

int default_wake_function(wait_queue_entry_t *curr, unsigned mode,
			  int wake_flags, void *key)
{
	return try_to_wake_up(curr->private, mode, wake_flags);
}

int sched_setscheduler_nocheck(struct task_struct *p, int policy,
			       const struct sched_param *param)
{
	return 0;
}

int __sched __cond_resched(void)
{
	if (should_resched(0)) {
		do {
			preempt_disable_notrace();
			__schedule(SM_PREEMPT);
			preempt_enable_no_resched_notrace();
		} while (need_resched());
		return 1;
	}

	barrier(); /* rcu_all_qs inlined */
	return 0;
}

/* sched_rr_get_interval replaced with COND_SYSCALL */

static void __init init_idle(struct task_struct *idle, int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	__sched_fork(0, idle);

	raw_spin_lock_irqsave(&idle->pi_lock, flags);
	raw_spin_rq_lock(rq);

	idle->__state = TASK_RUNNING;
	idle->se.exec_start = sched_clock();

	idle->flags |= PF_IDLE | PF_KTHREAD | PF_NO_SETAFFINITY;
	kthread_set_per_cpu(idle, cpu);

	rcu_read_lock();
	__set_task_cpu(idle, cpu);
	rcu_read_unlock();

	rq->idle = idle;
	rcu_assign_pointer(rq->curr, idle);
	idle->on_rq = TASK_ON_RQ_QUEUED;
	raw_spin_rq_unlock(rq);
	raw_spin_unlock_irqrestore(&idle->pi_lock, flags);

	init_idle_preempt_count(idle, cpu);

	idle->sched_class = &idle_sched_class;
}

void __init sched_init(void)
{
	BUG_ON(&idle_sched_class != &fair_sched_class + 1 ||
	       &fair_sched_class != &rt_sched_class + 1 ||
	       &rt_sched_class != &dl_sched_class + 1);

	wait_bit_init();

	{
		struct rq *rq = cpu_rq(0);
		raw_spin_lock_init(&rq->__lock);
		rq->nr_running = 0;
		init_cfs_rq(&rq->cfs);
	}

	set_load_weight(&init_task, false);

	mmgrab(&init_mm);
	enter_lazy_tlb(&init_mm, current);

	WARN_ON(!set_kthread_struct(current));

	init_idle(current, smp_processor_id());
}

const int sched_prio_to_weight[40] = {
	88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
	9548,  7620,  6100,  4904,  3906,  3121,  2501,	 1991,	1586,  1277,
	1024,  820,   655,   526,   423,   335,	  272,	 215,	172,   137,
	110,   87,    70,    56,    45,	   36,	  29,	 23,	18,    15,
};
