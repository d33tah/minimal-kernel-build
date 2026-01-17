
#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/hrtimer.h>
#include <linux/syscalls.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/sched/signal.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/rt.h>
#include <linux/sched/debug.h>
#include <linux/timer.h>

#include <linux/uaccess.h>

#include "tick-internal.h"

/* Minimal hrtimer_cpu_base - just the lock and minimal structure */
DEFINE_PER_CPU(struct hrtimer_cpu_base, hrtimer_bases) = {
	.lock = __RAW_SPIN_LOCK_UNLOCKED(hrtimer_bases.lock),
};

/* ktime_add_safe removed - never called */

void clock_was_set(unsigned int bases)
{
	/* Stubbed for minimal Hello World */
}

void clock_was_set_delayed(void)
{
	/* Stubbed for minimal Hello World */
}

/* hrtimer_start_range_ns, hrtimer_cancel, hrtimer_init, hrtimer_active removed - never called */

void hrtimer_run_queues(void)
{
	/* Stubbed for minimal Hello World */
}

/* hrtimers_prepare_cpu removed - CPU hotplug disabled, never called (~13 LOC) */

void __init hrtimers_init(void)
{
	/* Minimal init for Hello World */
}
