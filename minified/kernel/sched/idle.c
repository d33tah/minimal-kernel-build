#include <linux/sched.h>

#include "sched.h"

void cpu_startup_entry(enum cpuhp_state state)
{
	while (1) {
		set_thread_flag(TIF_POLLING_NRFLAG);
		while (!need_resched()) {
			rmb();
			local_irq_disable();
			arch_cpu_idle_enter();
			clear_thread_flag(TIF_POLLING_NRFLAG);
			smp_mb__after_atomic();
			if (unlikely(tif_need_resched())) {
				local_irq_enable();
			} else {
				arch_cpu_idle();
				raw_local_irq_disable();
				raw_local_irq_enable();
			}
		}
		clear_thread_flag(TIF_POLLING_NRFLAG);
		smp_mb__after_atomic();
		schedule_idle();
	}
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
}

struct task_struct *pick_next_task_idle(struct rq *rq)
{
	return rq->idle;
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
	.update_curr = update_curr_idle,
};
