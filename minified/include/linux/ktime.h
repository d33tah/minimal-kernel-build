#ifndef _LINUX_KTIME_H
#define _LINUX_KTIME_H
#include <linux/time.h>
#include <linux/jiffies.h>
#include <asm/bug.h>
typedef s64 ktime_t;
/* ktime_set removed - never called */
#define ktime_sub(lhs, rhs)	((lhs) - (rhs))
#define ktime_add_ns(kt, nsval)		((kt) + (nsval))
/* ktime_to_ns removed - inlined at single call site (returns kt unchanged) */
/* ktime_add_safe removed - never called */
#define LOW_RES_NSEC		TICK_NSEC
static inline ktime_t ns_to_ktime(u64 ns) { return ns; }
#include <linux/timekeeping.h>
#endif
