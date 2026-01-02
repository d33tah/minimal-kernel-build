#ifndef _LINUX_TIME64_H
#define _LINUX_TIME64_H

#include <linux/math64.h>

/* Inlined from vdso/time64.h */
#define MSEC_PER_SEC	1000L
#define USEC_PER_MSEC	1000L
#define NSEC_PER_USEC	1000L
#define NSEC_PER_MSEC	1000000L
#define USEC_PER_SEC	1000000L
#define NSEC_PER_SEC	1000000000L

typedef __s64 time64_t;
/* timeu64_t removed - unused */

#include <uapi/linux/time.h>

struct timespec64 {
	time64_t	tv_sec;			 
	long		tv_nsec;		 
};

#define TIME64_MAX			((s64)~((u64)1 << 63))
#define TIME64_MIN			(-TIME64_MAX - 1)

#define KTIME_MAX			((s64)~((u64)1 << 63))
#define KTIME_MIN			(-KTIME_MAX - 1)
#define KTIME_SEC_MAX			(KTIME_MAX / NSEC_PER_SEC)
#define KTIME_SEC_MIN			(KTIME_MIN / NSEC_PER_SEC)

/* timespec64_compare, timespec64_sub, timespec64_valid, timespec64_valid_settod,
   timespec64_to_ns, set_normalized_timespec64 removed - unused */

extern struct timespec64 ns_to_timespec64(const s64 nsec);

static __always_inline void timespec64_add_ns(struct timespec64 *a, u64 ns)
{
	a->tv_sec += __iter_div_u64_rem(a->tv_nsec + ns, NSEC_PER_SEC, &ns);
	a->tv_nsec = ns;
}


#endif  
