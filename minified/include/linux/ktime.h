#ifndef _LINUX_KTIME_H
#define _LINUX_KTIME_H

#include <linux/time.h>
#include <linux/jiffies.h>
#include <asm/bug.h>

typedef s64	ktime_t;

static inline ktime_t ktime_set(const s64 secs, const unsigned long nsecs) {
	if (unlikely(secs >= KTIME_SEC_MAX))
		return KTIME_MAX;

	return secs * NSEC_PER_SEC + (s64)nsecs; }

#define ktime_add_ns(kt, nsval)		((kt) + (nsval))

static inline ktime_t timespec64_to_ktime(struct timespec64 ts) {
	return ktime_set(ts.tv_sec, ts.tv_nsec); }

static inline s64 ktime_to_ns(const ktime_t kt) {
	return kt; }



/* BITS_PER_LONG == 32 */


/* Inlined from vdso/ktime.h */
#define LOW_RES_NSEC		TICK_NSEC

static inline ktime_t ns_to_ktime(u64 ns) {
	return ns; }

# include <linux/timekeeping.h>

#endif
