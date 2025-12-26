/* Simplified idle scheduler for minimal kernel */

#include <linux/sched/idle.h>
#include <linux/cpuidle.h>
#include <linux/tick.h>

#include "sched.h"

extern char __cpuidle_text_start[], __cpuidle_text_end[];

void __weak arch_cpu_idle_prepare(void)
{
}

void __weak arch_cpu_idle_exit(void)
{
}

void __cpuidle default_idle_call(void)
{
	if (current_clr_polling_and_test()) {
		local_irq_enable();
	} else {
		rcu_idle_enter();
		arch_cpu_idle();
		raw_local_irq_disable();
		rcu_idle_exit();
		raw_local_irq_enable();
	}
}

static void do_idle(void)
{
	__current_set_polling();
	tick_nohz_idle_enter();

	while (!need_resched()) {
		rmb();
		local_irq_disable();
		arch_cpu_idle_enter();

		default_idle_call();

		arch_cpu_idle_exit();
	}

	tick_nohz_idle_exit();
	__current_clr_polling();
	smp_mb__after_atomic();
	schedule_idle();
}

bool cpu_in_idle(unsigned long pc)
{
	return pc >= (unsigned long)__cpuidle_text_start &&
	       pc < (unsigned long)__cpuidle_text_end;
}

void cpu_startup_entry(enum cpuhp_state state)
{
	arch_cpu_idle_prepare();
	cpuhp_online_idle(state);
	while (1)
		do_idle();
}

static void check_preempt_curr_idle(struct rq *rq, struct task_struct *p,
				    int flags)
{
	resched_curr(rq);
}

static void put_prev_task_idle(struct rq *rq, struct task_struct *prev)
{
}

static void set_next_task_idle(struct rq *rq, struct task_struct *next,
			       bool first)
{
	update_idle_core(rq);
	schedstat_inc(rq->sched_goidle);
}

struct task_struct *pick_next_task_idle(struct rq *rq)
{
	struct task_struct *next = rq->idle;
	set_next_task_idle(rq, next, true);
	return next;
}

static void dequeue_task_idle(struct rq *rq, struct task_struct *p, int flags)
{
	raw_spin_rq_unlock_irq(rq);
	printk(KERN_ERR "bad: scheduling from the idle thread!\n");
	raw_spin_rq_lock_irq(rq);
}

static void task_tick_idle(struct rq *rq, struct task_struct *curr, int queued)
{
}

static void switched_to_idle(struct rq *rq, struct task_struct *p)
{
	BUG();
}

static void prio_changed_idle(struct rq *rq, struct task_struct *p, int oldprio)
{
	BUG();
}

static void update_curr_idle(struct rq *rq)
{
}

DEFINE_SCHED_CLASS(idle) = {
	.dequeue_task = dequeue_task_idle,
	.check_preempt_curr = check_preempt_curr_idle,
	.pick_next_task = pick_next_task_idle,
	.put_prev_task = put_prev_task_idle,
	.set_next_task = set_next_task_idle,
	.task_tick = task_tick_idle,
	.prio_changed = prio_changed_idle,
	.switched_to = switched_to_idle,
	.update_curr = update_curr_idle,
};
