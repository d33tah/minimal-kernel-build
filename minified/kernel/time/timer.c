/* kernel_stat.h, interrupt.h, pid_namespace.h, thread_info.h,
   time.h, cpu.h, syscalls.h, sched/signal.h, sched/sysctl.h,
   slab.h, compat.h, sysctl.h, uaccess.h,
   asm/unistd.h, asm/timex.h, asm/io.h removed - unused */
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/mm.h>
/* linux/swap.h removed - unused */
/* linux/kallsyms.h removed - unused */
/* irq_work.h removed - header is now empty */
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/sched/debug.h>
#include <asm/div64.h>

#include "tick-internal.h"

__visible u64 jiffies_64 __cacheline_aligned_in_smp = INITIAL_JIFFIES;

void init_timer_key(struct timer_list *timer, void (*func)(struct timer_list *),
		    unsigned int flags, const char *name,
		    struct lock_class_key *key)
{
	timer->entry.pprev = NULL;
	timer->function = func;
	timer->flags = flags | raw_smp_processor_id();
	/* lockdep_init_map removed - empty stub */
}
/* mod_timer, del_timer removed - never called */

void update_process_times(int user_tick)
{
	/* hrtimer_run_queues() removed - was empty stub */
	rcu_sched_clock_irq(user_tick);
	scheduler_tick();
}

signed long __sched schedule_timeout(signed long timeout)
{
	switch (timeout) {
	case MAX_SCHEDULE_TIMEOUT:
		schedule();
		goto out;
	default:
		if (timeout < 0) {
			printk(KERN_ERR
			       "schedule_timeout: wrong timeout value %lx\n",
			       timeout);
			__set_current_state(TASK_RUNNING);
			goto out;
		}
	}

	/* For minimal Hello World, just schedule and return 0 */
	schedule();

out:
	return timeout < 0 ? 0 : timeout;
}
/* schedule_timeout_interruptible removed - never called */

signed long __sched schedule_timeout_uninterruptible(signed long timeout)
{
	__set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}

void __init init_timers(void)
{
	/* Minimal init for Hello World */
}

/* msleep, msleep_interruptible removed - never called */
