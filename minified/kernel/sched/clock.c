/* Simplified scheduler clock for minimal kernel */

#include <linux/sched/clock.h>

notrace int sched_clock_stable(void)
{
	return 1;
}
/* sched_clock_init, sched_clock_cpu - removed, inlined in header */
