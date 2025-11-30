 
#ifndef _TIMEKEEPING_INTERNAL_H
#define _TIMEKEEPING_INTERNAL_H

#include <linux/clocksource.h>
#include <linux/spinlock.h>
#include <linux/time.h>

 
#define tk_debug_account_sleep_time(x)

static inline u64 clocksource_delta(u64 now, u64 last, u64 mask)
{
	u64 ret = (now - last) & mask;

	 
	return ret & ~(mask >> 1) ? 0 : ret;
}

 
extern raw_spinlock_t timekeeper_lock;

#endif  
