/* cpu.h, syscalls.h, interrupt.h, err.h, sched/signal.h, sched/sysctl.h,
   sched/rt.h, sched/debug.h, timer.h, uaccess.h removed - unused */
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/hrtimer.h>

#include "tick-internal.h"

/* Minimal hrtimer_cpu_base - just the lock and minimal structure */
DEFINE_PER_CPU(struct hrtimer_cpu_base, hrtimer_bases) = {
	.lock = __RAW_SPIN_LOCK_UNLOCKED(hrtimer_bases.lock),
};

/* ktime_add_safe, clock_was_set, clock_was_set_delayed removed - never called or empty stubs */

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
