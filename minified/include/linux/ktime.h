#ifndef _LINUX_KTIME_H
#define _LINUX_KTIME_H
#include <linux/time.h>
#include <linux/jiffies.h>
#include <asm/bug.h>
typedef s64 ktime_t;
#define ktime_add_ns(kt, nsval)		((kt) + (nsval))
#define LOW_RES_NSEC		TICK_NSEC
/* timekeeping.h inlined - content in clocksource.h */
#endif
