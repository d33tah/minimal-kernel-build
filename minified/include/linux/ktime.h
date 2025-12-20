#ifndef _LINUX_KTIME_H
#define _LINUX_KTIME_H

#include <linux/time.h>
#include <linux/jiffies.h>
#include <asm/bug.h>

typedef s64	ktime_t;

static inline ktime_t ktime_set(const s64 secs, const unsigned long nsecs)
{
	if (unlikely(secs >= KTIME_SEC_MAX))
		return KTIME_MAX;

	return secs * NSEC_PER_SEC + (s64)nsecs;
}

#define ktime_sub(lhs, rhs)	((lhs) - (rhs))

#define ktime_add(lhs, rhs)	((lhs) + (rhs))

#define ktime_add_unsafe(lhs, rhs)	((u64) (lhs) + (rhs))

#define ktime_add_ns(kt, nsval)		((kt) + (nsval))

#define ktime_sub_ns(kt, nsval)		((kt) - (nsval))

static inline ktime_t timespec64_to_ktime(struct timespec64 ts)
{
	return ktime_set(ts.tv_sec, ts.tv_nsec);
}

#define ktime_to_timespec64(kt)		ns_to_timespec64((kt))

static inline s64 ktime_to_ns(const ktime_t kt)
{
	return kt;
}

static inline int ktime_compare(const ktime_t cmp1, const ktime_t cmp2)
{
	if (cmp1 < cmp2)
		return -1;
	if (cmp1 > cmp2)
		return 1;
	return 0;
}


static inline bool ktime_before(const ktime_t cmp1, const ktime_t cmp2)
{
	return ktime_compare(cmp1, cmp2) < 0;
}

/* BITS_PER_LONG == 32 */

extern ktime_t ktime_add_safe(const ktime_t lhs, const ktime_t rhs);

/* Inlined from vdso/ktime.h */
#define LOW_RES_NSEC		TICK_NSEC
#define KTIME_LOW_RES		(LOW_RES_NSEC)

static inline ktime_t ns_to_ktime(u64 ns)
{
	return ns;
}

# include <linux/timekeeping.h>

#endif
