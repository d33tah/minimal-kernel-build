






#include <linux/sched/signal.h>
#include <linux/sched/clock.h>
#include <linux/sched/debug.h>


extern void sched_init(void);
extern void sched_init_smp(void);


#include <linux/mmu_context.h>




#include <asm/switch_to.h>

#include "sched.h"
#include "pelt.h"

DEFINE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);

__read_mostly int scheduler_running;

void raw_spin_rq_lock_nested(struct rq *rq, int subclass)
{
	/* SCHED_CORE off: rq lock is always rq->__lock, no core-cookie retry */
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

	lockdep_assert_held(&p->pi_lock);

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

static void update_rq_clock_task(struct rq *rq, s64 delta)
{

	s64 __maybe_unused steal = 0, irq_delta = 0;

	rq->clock_task += delta;
}

void update_rq_clock(struct rq *rq)
{
	s64 delta;

	lockdep_assert_rq_held(rq);

	if (rq->clock_update_flags & RQCF_ACT_SKIP)
		return;

	delta = sched_clock_cpu(cpu_of(rq)) - rq->clock;
	if (delta < 0)
		return;
	rq->clock += delta;
	update_rq_clock_task(rq, delta);
}

/*
 * wake_q batched-wakeup machinery (wake_q_add / wake_q_add_safe / wake_up_q
 * and the __wake_q_add helper) removed: zero callers on this boot. The wake_q
 * mechanism is only used by futex/rwsem/mutex contended wakeup batching, none
 * of which fires here (no DEFINE_WAKE_Q / wake_q_init / wake_q_add anywhere).
 */

void resched_curr(struct rq *rq)
{
	struct task_struct *curr = rq->curr;

	lockdep_assert_rq_held(rq);

	if (test_tsk_need_resched(curr))
		return;

	set_tsk_need_resched(curr);
	set_preempt_need_resched();
}

void resched_cpu(int cpu)
{
	/*
	 * RUNTIME-DEAD ANCHOR-STUB: the sole call site is call_rcu()'s
	 * `if (unlikely(is_idle_task(current))) resched_cpu(0)` (rcu/tiny.c).
	 * On this 1-shot boot call_rcu is never invoked from the idle task, so
	 * this branch is never taken (HIT=False, verified by exec-trace). The
	 * live reschedule path (resched_curr) stays reachable via fair.c/core.c.
	 */
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

static void
ttwu_do_activate(struct rq *rq, struct task_struct *p, int wake_flags,
		 struct rq_flags *rf)
{
	int en_flags = ENQUEUE_WAKEUP | ENQUEUE_NOCLOCK;

	lockdep_assert_rq_held(rq);

	activate_task(rq, p, en_flags);
	ttwu_do_wakeup(rq, p, wake_flags, rf);
}

static int ttwu_runnable(struct task_struct *p, int wake_flags)
{
	struct rq_flags rf;
	struct rq *rq;
	int ret = 0;

	rq = __task_rq_lock(p, &rf);
	if (task_on_rq_queued(p)) {
		
		update_rq_clock(rq);
		ttwu_do_wakeup(rq, p, wake_flags, &rf);
		ret = 1;
	}
	__task_rq_unlock(rq, &rf);

	return ret;
}

static void ttwu_queue(struct task_struct *p, int cpu, int wake_flags)
{
	struct rq *rq = cpu_rq(cpu);
	struct rq_flags rf;

	rq_lock(rq, &rf);
	update_rq_clock(rq);
	ttwu_do_activate(rq, p, wake_flags, &rf);
	rq_unlock(rq, &rf);
}

static __always_inline
bool ttwu_state_match(struct task_struct *p, unsigned int state, int *success)
{
	if (READ_ONCE(p->__state) & state) {
		*success = 1;
		return true;
	}

	return false;
}

static int
try_to_wake_up(struct task_struct *p, unsigned int state, int wake_flags)
{
	int cpu, success = 0;

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
	if (READ_ONCE(p->on_rq) && ttwu_runnable(p, wake_flags))
		goto out;

	cpu = task_cpu(p);

	ttwu_queue(p, cpu, wake_flags);
out:
	preempt_enable();

	return success;
}


static void __sched_fork(unsigned long clone_flags, struct task_struct *p)
{
	p->on_rq			= 0;

	p->se.on_rq			= 0;
	p->se.exec_start		= 0;
	p->se.sum_exec_runtime		= 0;
	p->se.prev_sum_exec_runtime	= 0;
	p->se.vruntime			= 0;
}


int sched_fork(unsigned long clone_flags, struct task_struct *p)
{
	__sched_fork(clone_flags, p);
	
	p->__state = TASK_NEW;

	/*
	 * sched_reset_on_fork is never set on this build (it has no setter
	 * tree-wide -- only ever read here and cleared), so the reset block
	 * was unreachable and has been removed.
	 */

	p->sched_class = &fair_sched_class;

	init_task_preempt_count(p);
	return 0;
}

void sched_cgroup_fork(struct task_struct *p, struct kernel_clone_args *kargs)
{
	unsigned long flags;

	
	raw_spin_lock_irqsave(&p->pi_lock, flags);

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

static inline void
prepare_lock_switch(struct rq *rq, struct task_struct *next, struct rq_flags *rf)
{
	
	rq_unpin_lock(rq, rf);
	spin_release(&__rq_lockp(rq)->dep_map, _THIS_IP_);
}

static inline void finish_lock_switch(struct rq *rq)
{
	
	spin_acquire(&__rq_lockp(rq)->dep_map, 0, 0, _THIS_IP_);
	raw_spin_rq_unlock_irq(rq);
}

#ifndef prepare_arch_switch
# define prepare_arch_switch(next)	do { } while (0)
#endif

#ifndef finish_arch_post_lock_switch
# define finish_arch_post_lock_switch()	do { } while (0)
#endif

static inline void
prepare_task_switch(struct rq *rq, struct task_struct *prev,
		    struct task_struct *next)
{
	prepare_arch_switch(next);
}

static struct rq *finish_task_switch(struct task_struct *prev)
	__releases(rq->lock)
{
	struct rq *rq = this_rq();
	struct mm_struct *mm = rq->prev_mm;
	unsigned int prev_state;

	
	if (WARN_ONCE(preempt_count() != 2*PREEMPT_DISABLE_OFFSET,
		      "corrupted preempt_count: %s/%d/0x%x\n",
		      current->comm, current->pid, preempt_count()))
		preempt_count_set(FORK_PREEMPT_COUNT);

	rq->prev_mm = NULL;

	
	prev_state = READ_ONCE(prev->__state);

	finish_lock_switch(rq);
	finish_arch_post_lock_switch();


	if (mm) {
		mmdrop_sched(mm);
	}
	if (unlikely(prev_state == TASK_DEAD)) {
		if (prev->sched_class->task_dead)
			prev->sched_class->task_dead(prev);

		
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

	calculate_sigpending();
}

static __always_inline struct rq *
context_switch(struct rq *rq, struct task_struct *prev,
	       struct task_struct *next, struct rq_flags *rf)
{
	prepare_task_switch(rq, prev, next);

	
	arch_start_context_switch(prev);

	
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

	rq->clock_update_flags &= ~(RQCF_ACT_SKIP|RQCF_REQ_SKIP);

	prepare_lock_switch(rq, next, rf);

	
	switch_to(prev, next, prev);
	barrier();

	return finish_task_switch(prev);
}




void scheduler_tick(void)
{
	int cpu = smp_processor_id();
	struct rq *rq = cpu_rq(cpu);
	struct task_struct *curr = rq->curr;
	struct rq_flags rf;
	unsigned long thermal_pressure;

	sched_clock_tick();

	rq_lock(rq, &rf);

	update_rq_clock(rq);
	thermal_pressure = arch_scale_thermal_pressure(cpu_of(rq));
	update_thermal_load_avg(rq_clock_thermal(rq), rq, thermal_pressure);
	curr->sched_class->task_tick(rq, curr, 0);

	rq_unlock(rq, &rf);
}

static inline void schedule_debug(struct task_struct *prev, bool preempt)
{

	if (unlikely(in_atomic_preempt_off())) {
		preempt_count_set(PREEMPT_DISABLED);
	}
}

static void put_prev_task_balance(struct rq *rq, struct task_struct *prev,
				  struct rq_flags *rf)
{

	put_prev_task(rq, prev);
}

static inline struct task_struct *
__pick_next_task(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
	const struct sched_class *class;
	struct task_struct *p;

	
	if (likely(!sched_class_above(prev->sched_class, &fair_sched_class) &&
		   rq->nr_running == rq->cfs.h_nr_running)) {

		p = pick_next_task_fair(rq, prev, rf);
		if (unlikely(p == RETRY_TASK))
			goto restart;

		
		if (!p) {
			put_prev_task(rq, prev);
			p = pick_next_task_idle(rq);
		}

		return p;
	}

restart:
	put_prev_task_balance(rq, prev, rf);

	for_each_class(class) {
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

#define SM_NONE			0x0
#define SM_PREEMPT		0x1

# define SM_MASK_PREEMPT	(~0U)

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

	schedule_debug(prev, !!sched_mode);

	local_irq_disable();
	rcu_note_context_switch(!!sched_mode);

	
	rq_lock(rq, &rf);
	smp_mb__after_spinlock();

	
	rq->clock_update_flags <<= 1;
	update_rq_clock(rq);


	prev_state = READ_ONCE(prev->__state);
	if (!(sched_mode & SM_MASK_PREEMPT) && prev_state) {
		if (signal_pending_state(prev_state, prev)) {
			WRITE_ONCE(prev->__state, TASK_RUNNING);
		} else {

			/* folded sole caller of deactivate_task() */
			prev->on_rq = 0; /* DEQUEUE_SLEEP set -> not MIGRATING */
			dequeue_task(rq, prev, DEQUEUE_SLEEP | DEQUEUE_NOCLOCK);
		}
	}

	next = pick_next_task(rq, prev, &rf);
	clear_tsk_need_resched(prev);
	clear_preempt_need_resched();

	if (likely(prev != next)) {
		RCU_INIT_POINTER(rq->curr, next);

		rq = context_switch(rq, prev, next, &rf);
	} else {
		rq->clock_update_flags &= ~(RQCF_ACT_SKIP|RQCF_REQ_SKIP);

		rq_unpin_lock(rq, &rf);
		raw_spin_rq_unlock_irq(rq);
	}
}

void __noreturn do_task_dead(void)
{
	
	set_special_state(TASK_DEAD);

	__schedule(SM_NONE);
	BUG();

	
	for (;;)
		cpu_relax();
}

static inline void sched_submit_work(struct task_struct *tsk)
{
	if (task_is_running(tsk))
		return;
}

asmlinkage __visible void __sched schedule(void)
{
	struct task_struct *tsk = current;

	sched_submit_work(tsk);
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

int default_wake_function(wait_queue_entry_t *curr, unsigned mode, int wake_flags,
			  void *key)
{
	WARN_ON_ONCE(IS_ENABLED(CONFIG_SCHED_DEBUG) && wake_flags & ~WF_SYNC);
	return try_to_wake_up(curr->private, mode, wake_flags);
}

/*
 * Runtime-dead anchor-stub: the only caller, kthread(), never runs on a
 * single-shot boot (no kthreads reach this line), so the full
 * dequeue/enqueue/setparam path (formerly _sched_setscheduler /
 * __sched_setscheduler, both private to this chain) is never executed.
 * Symbol kept link-live for the sched.h extern + kthread.c:139 reference.
 */
int sched_setscheduler_nocheck(struct task_struct *p, int policy,
			       const struct sched_param *param)
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
	sched_init_granularity();
}


void __init sched_init(void)
{
	int i;


	BUG_ON(&idle_sched_class != &fair_sched_class + 1);

	for_each_possible_cpu(i) {
		struct rq *rq;

		rq = cpu_rq(i);
		raw_spin_lock_init(&rq->__lock);
		rq->nr_running = 0;
		init_cfs_rq(&rq->cfs);
	}

	set_load_weight(&init_task, false);

	
	mmgrab(&init_mm);
	enter_lazy_tlb(&init_mm, current);

	
	WARN_ON(!set_kthread_struct(current));

	
	init_idle(current, smp_processor_id());

	init_sched_fair_class();

	scheduler_running = 1;
}

const int sched_prio_to_weight[40] = {
      88761,     71755,     56483,     46273,     36291,
      29154,     23254,     18705,     14949,     11916,
       9548,      7620,      6100,      4904,      3906,
       3121,      2501,      1991,      1586,      1277,
       1024,       820,       655,       526,       423,
        335,       272,       215,       172,       137,
        110,        87,        70,        56,        45,
         36,        29,        23,        18,        15,
};

const u32 sched_prio_to_wmult[40] = {
      48388,     59856,     76040,     92818,    118348,
     147320,    184698,    229616,    287308,    360437,
     449829,    563644,    704093,    875809,   1099582,
    1376151,   1717300,   2157191,   2708050,   3363326,
    4194304,   5237765,   6557202,   8165337,  10153587,
   12820798,  15790321,  19976592,  24970740,  31350126,
   39045157,  49367440,  61356676,  76695844,  95443717,
  119304647, 148102320, 186737708, 238609294, 286331153,
};

