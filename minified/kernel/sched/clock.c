/* Simplified scheduler clock for minimal kernel */

#include <linux/sched/clock.h>

notrace int sched_clock_stable(void)
{
	return 1;
}
void __init sched_clock_init(void)
{
}
notrace u64 sched_clock_cpu(int cpu)
{
	return sched_clock();
}
