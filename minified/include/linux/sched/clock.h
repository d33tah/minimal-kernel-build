#ifndef _LINUX_SCHED_CLOCK_H
#define _LINUX_SCHED_CLOCK_H

#include <linux/types.h>

extern unsigned long long notrace sched_clock(void);

static inline u64 local_clock(void)
{
	return sched_clock();
}

#endif
