#ifndef _LINUX_SCHED_CLOCK_H
#define _LINUX_SCHED_CLOCK_H

#include <linux/smp.h>

extern unsigned long long notrace sched_clock(void);
extern u64 sched_clock_cpu(int cpu);
extern int sched_clock_stable(void);
/* sched_clock_init, clear_sched_clock_stable, sched_clock_tick, sched_clock_tick_stable
   removed - declared but never called */

static inline u64 local_clock(void)
{
	return sched_clock_cpu(raw_smp_processor_id());
}

#endif
