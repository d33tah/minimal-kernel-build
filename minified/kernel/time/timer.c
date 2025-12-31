
#include <linux/kernel_stat.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/pid_namespace.h>
#include <linux/notifier.h>
#include <linux/thread_info.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/posix-timers.h>
#include <linux/cpu.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <linux/tick.h>
#include <linux/kallsyms.h>
#include <linux/irq_work.h>
#include <linux/sched/signal.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/debug.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/sysctl.h>

#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/div64.h>
#include <asm/timex.h>
#include <asm/io.h>

#include "tick-internal.h"

__visible u64 jiffies_64 __cacheline_aligned_in_smp = INITIAL_JIFFIES;

/* Minimal timer_base - reduced wheel for Hello World */
#define LVL_BITS 4
#define LVL_SIZE (1UL << LVL_BITS)
#define LVL_DEPTH 2
#define WHEEL_SIZE (LVL_SIZE * LVL_DEPTH)
#define NR_BASES 1
#define BASE_STD 0

struct timer_base {
	raw_spinlock_t lock;
	struct timer_list *running_timer;
	unsigned long clk;
	unsigned long next_expiry;
	unsigned int cpu;
	bool next_expiry_recalc;
	bool is_idle;
	bool timers_pending;
	DECLARE_BITMAP(pending_map, WHEEL_SIZE);
	struct hlist_head vectors[WHEEL_SIZE];
} ____cacheline_aligned;

static DEFINE_PER_CPU(struct timer_base, timer_bases[NR_BASES]);

void init_timer_key(struct timer_list *timer, void (*func)(struct timer_list *),
		    unsigned int flags, const char *name,
		    struct lock_class_key *key)
{
	timer->entry.pprev = NULL;
	timer->function = func;
	timer->flags = flags | raw_smp_processor_id();
	lockdep_init_map(&timer->lockdep_map, name, key, 0);
}

int mod_timer(struct timer_list *timer, unsigned long expires)
{
	/* Stubbed for minimal Hello World */
	return 0;
}

int del_timer(struct timer_list *timer)
{
	/* Stubbed for minimal Hello World */
	return 0;
}

static void run_local_timers(void)
{
	hrtimer_run_queues();
}

void update_process_times(int user_tick)
{
	run_local_timers();
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

signed long __sched schedule_timeout_interruptible(signed long timeout)
{
	__set_current_state(TASK_INTERRUPTIBLE);
	return schedule_timeout(timeout);
}

signed long __sched schedule_timeout_uninterruptible(signed long timeout)
{
	__set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}

void __init init_timers(void)
{
	/* Minimal init for Hello World */
}

void msleep(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs) + 1;
	while (timeout)
		timeout = schedule_timeout_uninterruptible(timeout);
}

unsigned long msleep_interruptible(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs) + 1;
	while (timeout && !signal_pending(current))
		timeout = schedule_timeout_interruptible(timeout);
	return jiffies_to_msecs(timeout);
}
