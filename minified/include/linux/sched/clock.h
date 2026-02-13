#ifndef _LINUX_SCHED_CLOCK_H
#define _LINUX_SCHED_CLOCK_H

#include <linux/smp.h>

extern unsigned long long notrace sched_clock(void);

/* sched_clock_cpu just calls sched_clock(), inlined here */
static inline u64 sched_clock_cpu(int cpu)
{
	return sched_clock();
}

#endif
