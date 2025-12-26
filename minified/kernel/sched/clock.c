/* Simplified scheduler clock for minimal kernel
 * Just use sched_clock() directly - no GTOD synchronization needed
 */

#include <linux/sched/clock.h>
#include <linux/jump_label.h>

static DEFINE_STATIC_KEY_FALSE(sched_clock_running);
static DEFINE_STATIC_KEY_FALSE(__sched_clock_stable);

__read_mostly u64 __sched_clock_offset;

notrace int sched_clock_stable(void)
{
	return 1; /* Always stable for minimal kernel */
}

void __init sched_clock_init(void)
{
	static_branch_enable(&__sched_clock_stable);
	static_branch_inc(&sched_clock_running);
}

static int __init sched_clock_init_late(void)
{
	static_branch_inc(&sched_clock_running);
	return 0;
}
late_initcall(sched_clock_init_late);

notrace u64 sched_clock_cpu(int cpu)
{
	return sched_clock();
}

notrace void sched_clock_tick(void)
{
}

notrace void sched_clock_tick_stable(void)
{
}

notrace void clear_sched_clock_stable(void)
{
}
