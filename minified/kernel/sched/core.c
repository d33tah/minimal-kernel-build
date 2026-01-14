

#include <linux/sched/signal.h>

#include <linux/wait_bit.h>
#include <linux/jiffies.h>

#include <linux/hardirq.h>

#include <linux/sched/clock.h>

#include <linux/sched/cputime.h>
#include <linux/sched/debug.h>

extern void sched_init(void);
extern void sched_init_smp(void);
#include <linux/sched/mm.h>

#include <linux/sched/rt.h>

/* Removed: delayacct_blkio_start/end - empty stubs */
#include <linux/init_task.h>
#include <linux/interrupt.h>

#include <linux/mmu_context.h>
/* nospec.h removed - unused, psi_init stub removed */
#include <linux/sched/wake_q.h>
#include <linux/slab.h>
#include <linux/syscalls.h>

#include <asm/switch_to.h>
#include <asm/tlb.h>

#include "sched.h"
#include "stats.h"
#include "pelt.h"

/* Removed: io_wq_worker_sleeping/running, sched_core_enqueue/dequeue - empty stubs */

DEFINE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);

__read_mostly int scheduler_running;

void raw_spin_rq_lock_nested(struct rq *rq, int subclass)
{
	/* sched_core_disabled() always true - dead loop removed */
	preempt_disable();
	raw_spin_lock_nested(&rq->__lock, subclass);
	preempt_enable_no_resched();
}

void raw_spin_rq_unlock(struct rq *rq)
{
	raw_spin_unlock(rq_lockp(rq));
}

struct rq *__task_rq_lock(struct task_struct *p, struct rq_flags *rf)
	__acquires(rq->lock)
{
	struct rq *rq;

	for (;;) {
		rq = task_rq(p);
		raw_spin_rq_lock(rq);
		if (likely(rq == task_rq(p) && !task_on_rq_migrating(p))) {
			rq_pin_lock(rq, rf);
			return rq;
		}
		raw_spin_rq_unlock(rq);

		while (unlikely(task_on_rq_migrating(p)))
			cpu_relax();
	}
}

struct rq *task_rq_lock(struct task_struct *p, struct rq_flags *rf)
	__acquires(p->pi_lock) __acquires(rq->lock)
{
	struct rq *rq;

	for (;;) {
		raw_spin_lock_irqsave(&p->pi_lock, rf->flags);
		rq = task_rq(p);
		raw_spin_rq_lock(rq);

		if (likely(rq == task_rq(p) && !task_on_rq_migrating(p))) {
			rq_pin_lock(rq, rf);
			return rq;
		}
		raw_spin_rq_unlock(rq);
		raw_spin_unlock_irqrestore(&p->pi_lock, rf->flags);

		while (unlikely(task_on_rq_migrating(p)))
			cpu_relax();
	}
}

void update_rq_clock(struct rq *rq)
{
	s64 delta;

	if (rq->clock_update_flags & RQCF_ACT_SKIP)
		return;

	delta = sched_clock_cpu(cpu_of(rq)) - rq->clock;
	if (delta < 0)
		return;
	rq->clock += delta;
	rq->clock_task += delta;
	update_rq_clock_pelt(rq, delta);
}

/* fetch_or macro removed - only used by removed set_nr_and_not_polling */

static bool __wake_q_add(struct wake_q_head *head, struct task_struct *task)
{
	struct wake_q_node *node = &task->wake_q;

	smp_mb__before_atomic();
	if (unlikely(cmpxchg_relaxed(&node->next, NULL, WAKE_Q_TAIL)))
		return false;

	*head->lastp = node;
	head->lastp = &node->next;
	return true;
}

void wake_q_add(struct wake_q_head *head, struct task_struct *task)
{
	if (__wake_q_add(head, task))
		get_task_struct(task);
}

void wake_q_add_safe(struct wake_q_head *head, struct task_struct *task)
{
	if (!__wake_q_add(head, task))
		put_task_struct(task);
}

void wake_up_q(struct wake_q_head *head)
{
	struct wake_q_node *node = head->first;

	while (node != WAKE_Q_TAIL) {
		struct task_struct *task;

		task = container_of(node, struct task_struct, wake_q);

		node = node->next;
		task->wake_q.next = NULL;

		wake_up_process(task);
		put_task_struct(task);
	}
}

void resched_curr(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	int cpu;

	if (test_tsk_need_resched(curr))
		return;

	cpu = cpu_of(rq);

	if (cpu == smp_processor_id()) {
		set_tsk_need_resched(curr);
		set_preempt_need_resched();
		return;
	}

	/* smp_send_reschedule removed - empty stub in UP config */
}

void resched_cpu(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	raw_spin_rq_lock_irqsave(rq, flags);
	if (cpu_online(cpu) || cpu == smp_processor_id())
		resched_curr(rq);
	raw_spin_rq_unlock_irqrestore(rq, flags);
}

static void set_load_weight(struct task_struct *p, bool update_load)
{
	int prio = p->static_prio - MAX_RT_PRIO;
	struct load_weight *load = &p->se.load;

	if (task_has_idle_policy(p)) {
		load->weight = scale_load(WEIGHT_IDLEPRIO);
		load->inv_weight = WMULT_IDLEPRIO;
		return;
	}

	if (update_load && p->sched_class == &fair_sched_class) {
		reweight_task(p, prio);
	} else {
		load->weight = scale_load(sched_prio_to_weight[prio]);
		load->inv_weight = sched_prio_to_wmult[prio];
	}
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

void activate_task(struct rq *rq, struct task_struct *p, int flags)
{
	enqueue_task(rq, p, flags);

	p->on_rq = TASK_ON_RQ_QUEUED;
}

void deactivate_task(struct rq *rq, struct task_struct *p, int flags)
{
	p->on_rq = (flags & DEQUEUE_SLEEP) ? 0 : TASK_ON_RQ_MIGRATING;

	dequeue_task(rq, p, flags);
}

static inline int __normal_prio(int policy, int rt_prio, int nice)
{
	int prio;

	if (dl_policy(policy))
		prio = MAX_DL_PRIO - 1;
	else if (rt_policy(policy))
		prio = MAX_RT_PRIO - 1 - rt_prio;
	else
		prio = NICE_TO_PRIO(nice);

	return prio;
}

/* check_class_changed inlined into __sched_setscheduler */

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

static __always_inline bool ttwu_state_match(struct task_struct *p,
					     unsigned int state, int *success)
{
	if (READ_ONCE(p->__state) & state) {
		*success = 1;
		return true;
	}

	return false;
}

static int try_to_wake_up(struct task_struct *p, unsigned int state,
			  int wake_flags)
{
	int cpu, success = 0;
	struct rq_flags rf;
	struct rq *rq;

	preempt_disable();
	if (p == current) {
		if (!ttwu_state_match(p, state, &success))
			goto out;

		WRITE_ONCE(p->__state, TASK_RUNNING);

		goto out;
	}

	if (READ_ONCE(p->__state) == TASK_RUNNING)
		goto out;

	smp_rmb();
	/* Inlined ttwu_runnable */
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

	/* Inlined ttwu_queue */
	{
		struct rq *rq = cpu_rq(cpu);
		rq_lock(rq, &rf);
		update_rq_clock(rq);
		if (p->in_iowait)
			atomic_dec(&task_rq(p)->nr_iowait);
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
	p->se.prev_sum_exec_runtime = 0;
	p->se.vruntime = 0;

	/* p->rt.run_list, timeout, time_slice, on_rq, on_list removed - write-only fields */
	/* init_numa_balancing - empty stub removed */
}

int sched_fork(unsigned long clone_flags, struct task_struct *p)
{
	__sched_fork(clone_flags, p);

	p->__state = TASK_NEW;

	p->prio = current->normal_prio;
	if (unlikely(p->sched_reset_on_fork)) {
		if (task_has_dl_policy(p) || task_has_rt_policy(p)) {
			p->policy = SCHED_NORMAL;
			p->static_prio = NICE_TO_PRIO(0);
			p->rt_priority = 0;
		} else if (PRIO_TO_NICE(p->static_prio) < 0)
			p->static_prio = NICE_TO_PRIO(0);

		p->prio = p->normal_prio = p->static_prio;
		set_load_weight(p, false);

		p->sched_reset_on_fork = 0;
	}

	/* dl_prio always returns false since MAX_DL_PRIO=0 */
	if (rt_prio(p->prio))
		p->sched_class = &rt_sched_class;
	else
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

void sched_post_fork(struct task_struct *p)
{
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

static inline void prepare_lock_switch(struct rq *rq, struct task_struct *next,
				       struct rq_flags *rf)
{
	rq_unpin_lock(rq, rf);
}

static inline void finish_lock_switch(struct rq *rq)
{
	raw_spin_rq_unlock_irq(rq);
}

#ifndef prepare_arch_switch
#define prepare_arch_switch(next) \
	do {                      \
	} while (0)
#endif

static inline void prepare_task_switch(struct rq *rq, struct task_struct *prev,
				       struct task_struct *next)
{
	if (unlikely(current->kmap_ctrl.idx))
		__kmap_local_sched_out();
	prepare_arch_switch(next);
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

	finish_lock_switch(rq);

	if (unlikely(current->kmap_ctrl.idx))
		__kmap_local_sched_in();

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

	if (current->set_child_tid)
		put_user(task_pid_vnr(current), current->set_child_tid);

	calculate_sigpending();
}

static __always_inline struct rq *context_switch(struct rq *rq,
						 struct task_struct *prev,
						 struct task_struct *next,
						 struct rq_flags *rf)
{
	prepare_task_switch(rq, prev, next);

	if (!next->mm) {
		enter_lazy_tlb(prev->active_mm, next);

		next->active_mm = prev->active_mm;
		if (prev->mm)
			mmgrab(prev->active_mm);
		else
			prev->active_mm = NULL;
	} else {
		membarrier_switch_mm(rq, prev->active_mm, next->mm);

		switch_mm_irqs_off(prev->active_mm, next->mm, next);

		if (!prev->mm) {
			rq->prev_mm = prev->active_mm;
			prev->active_mm = NULL;
		}
	}

	rq->clock_update_flags &= ~(RQCF_ACT_SKIP | RQCF_REQ_SKIP);

	prepare_lock_switch(rq, next, rf);

	switch_to(prev, next, prev);
	barrier();

	return finish_task_switch(prev);
}

/* nr_running simplified - single CPU */
unsigned int nr_running(void)
{
	return cpu_rq(0)->nr_running;
}

unsigned int nr_iowait_cpu(int cpu)
{
	return atomic_read(&cpu_rq(cpu)->nr_iowait);
}

/* nr_iowait simplified - single CPU */
unsigned int nr_iowait(void)
{
	return nr_iowait_cpu(0);
}

DEFINE_PER_CPU(struct kernel_stat, kstat);

/* cpu_resched_latency removed - always returned 0, only used by removed code */

void scheduler_tick(void)
{
	int cpu = smp_processor_id();
	struct rq *rq = cpu_rq(cpu);
	struct task_struct *curr = rq->curr;
	struct rq_flags rf;

	rq_lock(rq, &rf);

	update_rq_clock(rq);
	/* thermal_pressure/update_thermal_load_avg removed - always 0 */
	curr->sched_class->task_tick(rq, curr, 0);
	rq_unlock(rq, &rf);
}

static inline struct task_struct *
__pick_next_task(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
	const struct sched_class *class;
	struct task_struct *p;

	if (likely(!sched_class_above(prev->sched_class, &fair_sched_class) &&
		   rq->nr_running == rq->cfs.h_nr_running)) {
		p = pick_next_task_fair(rq, prev, rf);
		/* RETRY_TASK check removed - pick_next_task_fair never returns it */

		if (!p) {
			put_prev_task(rq, prev);
			p = pick_next_task_idle(rq);
		}

		return p;
	}

	/* restart label removed - RETRY_TASK never returned */
	put_prev_task(rq, prev);

	for_each_class(class)
	{
		p = class->pick_next_task(rq);
		if (p)
			return p;
	}

	BUG();
}

static struct task_struct *
pick_next_task(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
	return __pick_next_task(rq, prev, rf);
}

#define SM_NONE 0x0
#define SM_PREEMPT 0x1

#define SM_MASK_PREEMPT (~0U)

static void __sched notrace __schedule(unsigned int sched_mode)
{
	struct task_struct *prev, *next;
	/* switch_count removed - write-only field */
	unsigned long prev_state;
	struct rq_flags rf;
	struct rq *rq;
	int cpu;

	cpu = smp_processor_id();
	rq = cpu_rq(cpu);
	prev = rq->curr;

	/* Inlined schedule_debug */
	if (unlikely(in_atomic_preempt_off())) {
		if (panic_on_warn)
			panic("scheduling while atomic\n");
		preempt_count_set(PREEMPT_DISABLED);
	}
	local_irq_disable();
	rcu_note_context_switch(!!sched_mode);

	rq_lock(rq, &rf);
	rq->clock_update_flags <<= 1;
	update_rq_clock(rq);

	/* switch_count pointing to nivcsw/nvcsw removed - write-only fields */

	prev_state = READ_ONCE(prev->__state);
	if (!(sched_mode & SM_MASK_PREEMPT) && prev_state) {
		if (signal_pending_state(prev_state, prev)) {
			WRITE_ONCE(prev->__state, TASK_RUNNING);
		} else {
			/* sched_contributes_to_load and nr_uninterruptible removed - write-only */

			deactivate_task(rq, prev,
					DEQUEUE_SLEEP | DEQUEUE_NOCLOCK);

			if (prev->in_iowait)
				atomic_inc(&rq->nr_iowait);
		}
	}

	next = pick_next_task(rq, prev, &rf);
	clear_tsk_need_resched(prev);
	clear_preempt_need_resched();

	if (likely(prev != next)) {
		RCU_INIT_POINTER(rq->curr, next);
		/* ++*switch_count removed - write-only field */

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

/* sched_submit_work inlined - now essentially empty after stubs removed */

asmlinkage __visible void __sched schedule(void)
{
	/* sched_submit_work inlined: wq_worker_sleeping, tsk_is_pi_blocked,
	 * blk_flush_plug all removed as stubs; remaining check is no-op */
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

static void __sched notrace preempt_schedule_common(void)
{
	do {
		preempt_disable_notrace();
		__schedule(SM_PREEMPT);
		preempt_enable_no_resched_notrace();

	} while (need_resched());
}

asmlinkage __visible void __sched preempt_schedule_irq(void)
{
	BUG_ON(preempt_count() || !irqs_disabled());

	do {
		preempt_disable();
		local_irq_enable();
		__schedule(SM_PREEMPT);
		local_irq_disable();
		sched_preempt_enable_no_resched();
	} while (need_resched());
}

int default_wake_function(wait_queue_entry_t *curr, unsigned mode,
			  int wake_flags, void *key)
{
	return try_to_wake_up(curr->private, mode, wake_flags);
}

/* __setscheduler_prio inlined into __sched_setscheduler */

#ifdef __ARCH_WANT_SYS_NICE

SYSCALL_DEFINE1(nice, int, increment)
{
	/* Stub: nice not needed for minimal kernel */
	return 0;
}

#endif

#define SETPARAM_POLICY -1

static int __sched_setscheduler(struct task_struct *p,
				const struct sched_attr *attr, bool user,
				bool pi)
{
	/* Minimal stub: simplified scheduler parameter setting */
	int policy = attr->sched_policy;
	int retval, oldprio, newprio, queued, running;
	const struct sched_class *prev_class;
	struct rq_flags rf;
	int queue_flags = DEQUEUE_SAVE | DEQUEUE_MOVE | DEQUEUE_NOCLOCK;
	struct rq *rq;

	/* Basic validation only */
	if (policy < 0)
		policy = p->policy;
	else if (!valid_policy(policy))
		return -EINVAL;

	if (attr->sched_priority > MAX_RT_PRIO - 1)
		return -EINVAL;

	/* Skip all permission checks for minimal kernel */

	rq = task_rq_lock(p, &rf);
	update_rq_clock(rq);

	if (p == rq->stop) {
		retval = -EINVAL;
		goto unlock;
	}

	/* Check if already set */
	if (policy == p->policy &&
	    (!fair_policy(policy) || attr->sched_nice == task_nice(p)) &&
	    (!rt_policy(policy) || attr->sched_priority == p->rt_priority)) {
		retval = 0;
		goto unlock;
	}

	oldprio = p->prio;
	newprio = __normal_prio(policy, attr->sched_priority, attr->sched_nice);

	queued = task_on_rq_queued(p);
	running = task_current(rq, p);
	if (queued)
		dequeue_task(rq, p, queue_flags);
	if (running)
		put_prev_task(rq, p);

	prev_class = p->sched_class;

	if (!(attr->sched_flags & SCHED_FLAG_KEEP_PARAMS)) {
		/* Inlined __setscheduler_params */
		int sched_policy = attr->sched_policy;
		if (sched_policy == SETPARAM_POLICY)
			sched_policy = p->policy;
		p->policy = sched_policy;
		if (fair_policy(sched_policy))
			p->static_prio = NICE_TO_PRIO(attr->sched_nice);
		p->rt_priority = attr->sched_priority;
		p->normal_prio = __normal_prio(p->policy, p->rt_priority,
					       PRIO_TO_NICE(p->static_prio));
		set_load_weight(p, true);
		/* Inlined __setscheduler_prio - dl_prio always false */
		if (rt_prio(newprio))
			p->sched_class = &rt_sched_class;
		else
			p->sched_class = &fair_sched_class;
		p->prio = newprio;
	}
	if (queued) {
		if (oldprio < p->prio)
			queue_flags |= ENQUEUE_HEAD;
		enqueue_task(rq, p, queue_flags);
	}
	if (running)
		set_next_task(rq, p);

	/* Inlined check_class_changed */
	if (prev_class != p->sched_class) {
		if (prev_class->switched_from)
			prev_class->switched_from(rq, p);
		p->sched_class->switched_to(rq, p);
	} else if (oldprio != p->prio) /* dl_task always false */
		p->sched_class->prio_changed(rq, p, oldprio);

	task_rq_unlock(rq, p, &rf);

	return 0;

unlock:
	task_rq_unlock(rq, p, &rf);
	return retval;
}

static int _sched_setscheduler(struct task_struct *p, int policy,
			       const struct sched_param *param, bool check)
{
	struct sched_attr attr = {
		.sched_policy = policy,
		.sched_priority = param->sched_priority,
		.sched_nice = PRIO_TO_NICE(p->static_prio),
	};

	if ((policy != SETPARAM_POLICY) && (policy & SCHED_RESET_ON_FORK)) {
		attr.sched_flags |= SCHED_FLAG_RESET_ON_FORK;
		policy &= ~SCHED_RESET_ON_FORK;
		attr.sched_policy = policy;
	}

	return __sched_setscheduler(p, &attr, check, true);
}

int sched_setscheduler(struct task_struct *p, int policy,
		       const struct sched_param *param)
{
	return _sched_setscheduler(p, policy, param, true);
}

int sched_setattr(struct task_struct *p, const struct sched_attr *attr)
{
	return __sched_setscheduler(p, attr, true, true);
}

int sched_setscheduler_nocheck(struct task_struct *p, int policy,
			       const struct sched_param *param)
{
	return _sched_setscheduler(p, policy, param, false);
}

void sched_set_fifo(struct task_struct *p)
{
	struct sched_param sp = { .sched_priority = MAX_RT_PRIO / 2 };
	WARN_ON_ONCE(sched_setscheduler_nocheck(p, SCHED_FIFO, &sp) != 0);
}

SYSCALL_DEFINE3(sched_setscheduler, pid_t, pid, int, policy,
		struct sched_param __user *, param)
{
	/* Stub: sched_setscheduler not needed for minimal kernel */
	return 0;
}

SYSCALL_DEFINE2(sched_setparam, pid_t, pid, struct sched_param __user *, param)
{
	/* Stub: sched_setparam not needed for minimal kernel */
	return 0;
}

SYSCALL_DEFINE3(sched_setattr, pid_t, pid, struct sched_attr __user *, uattr,
		unsigned int, flags)
{
	/* Stubbed: sched_setattr not needed for minimal kernel */
	return -ENOSYS;
}

SYSCALL_DEFINE1(sched_getscheduler, pid_t, pid)
{
	/* Stub: sched_getscheduler not needed for minimal kernel */
	return SCHED_NORMAL;
}

SYSCALL_DEFINE2(sched_getparam, pid_t, pid, struct sched_param __user *, param)
{
	/* Stub: sched_getparam not needed for Hello World */
	return 0;
}

SYSCALL_DEFINE4(sched_getattr, pid_t, pid, struct sched_attr __user *, uattr,
		unsigned int, usize, unsigned int, flags)
{
	/* Stubbed: sched_getattr not needed for minimal kernel */
	return -ENOSYS;
}

long sched_setaffinity(pid_t pid, const struct cpumask *in_mask)
{
	/* Stub: not needed for minimal kernel */
	return 0;
}

/* Stub: setaffinity not needed for single-CPU minimal kernel */
SYSCALL_DEFINE3(sched_setaffinity, pid_t, pid, unsigned int, len,
		unsigned long __user *, user_mask_ptr)
{
	return 0;
}

long sched_getaffinity(pid_t pid, struct cpumask *mask)
{
	/* Stub: not needed for minimal kernel */
	return -ESRCH;
}

SYSCALL_DEFINE3(sched_getaffinity, pid_t, pid, unsigned int, len,
		unsigned long __user *, user_mask_ptr)
{
	/* Stub: just return success for minimal kernel */
	return -ENOSYS;
}

/* Stub: sched_yield not needed for Hello World */
SYSCALL_DEFINE0(sched_yield)
{
	return 0;
}

int __sched __cond_resched(void)
{
	if (should_resched(0)) {
		preempt_schedule_common();
		return 1;
	}

	rcu_all_qs();
	return 0;
}

void __sched io_schedule(void)
{
	int old_iowait = current->in_iowait;

	current->in_iowait = 1;
	schedule();
	current->in_iowait = old_iowait;
}

SYSCALL_DEFINE1(sched_get_priority_max, int, policy)
{
	/* Stub: just return 0 for minimal kernel */
	return 0;
}

SYSCALL_DEFINE1(sched_get_priority_min, int, policy)
{
	/* Stub: just return 0 for minimal kernel */
	return 0;
}

/* Stub: sched_rr_get_interval not needed for Hello World */
SYSCALL_DEFINE2(sched_rr_get_interval, pid_t, pid,
		struct __kernel_timespec __user *, interval)
{
	return -ENOSYS;
}

void __init init_idle(struct task_struct *idle, int cpu)
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

void __init sched_init_smp(void)
{
}

DECLARE_PER_CPU(cpumask_var_t, load_balance_mask);
DECLARE_PER_CPU(cpumask_var_t, select_idle_mask);

void __init sched_init(void)
{
	BUG_ON(&idle_sched_class != &fair_sched_class + 1 ||
	       &fair_sched_class != &rt_sched_class + 1 ||
	       &rt_sched_class != &dl_sched_class + 1);

	wait_bit_init();

	/* dead if (ptr) block removed - ptr was always 0 */

	init_rt_bandwidth(&def_rt_bandwidth, global_rt_period(),
			  global_rt_runtime());

	/* for_each_possible_cpu simplified - single CPU */
	{
		struct rq *rq = cpu_rq(0);
		raw_spin_lock_init(&rq->__lock);
		rq->nr_running = 0;
		init_cfs_rq(&rq->cfs);
		init_rt_rq(&rq->rt);
		init_dl_rq(&rq->dl);
		rq->rt.rt_runtime = def_rt_bandwidth.rt_runtime;
		atomic_set(&rq->nr_iowait, 0);
	}

	set_load_weight(&init_task, false);

	mmgrab(&init_mm);
	enter_lazy_tlb(&init_mm, current);

	WARN_ON(!set_kthread_struct(current));

	init_idle(current, smp_processor_id());

	calc_load_update = jiffies + LOAD_FREQ;

	scheduler_running = 1;
}

const int sched_prio_to_weight[40] = {
	88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
	9548,  7620,  6100,  4904,  3906,  3121,  2501,	 1991,	1586,  1277,
	1024,  820,   655,   526,   423,   335,	  272,	 215,	172,   137,
	110,   87,    70,    56,    45,	   36,	  29,	 23,	18,    15,
};

const u32 sched_prio_to_wmult[40] = {
	48388,	   59856,     76040,	 92818,	    118348,   147320,
	184698,	   229616,    287308,	 360437,    449829,   563644,
	704093,	   875809,    1099582,	 1376151,   1717300,  2157191,
	2708050,   3363326,   4194304,	 5237765,   6557202,  8165337,
	10153587,  12820798,  15790321,	 19976592,  24970740, 31350126,
	39045157,  49367440,  61356676,	 76695844,  95443717, 119304647,
	148102320, 186737708, 238609294, 286331153,
};
