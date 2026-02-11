#ifndef _LINUX_TIME64_H
#define _LINUX_TIME64_H

#include <linux/math64.h>

/* Inlined from vdso/time64.h */
/* MSEC_PER_SEC removed - only used in _msecs_to_jiffies which was removed */
/* USEC_PER_MSEC removed - unused */
#define NSEC_PER_USEC	1000L
#define NSEC_PER_MSEC	1000000L
#define NSEC_PER_SEC	1000000000L

typedef __s64 time64_t;
/* timeu64_t removed - unused */

#include <uapi/linux/time.h>

struct timespec64 {
	time64_t	tv_sec;			 
	long		tv_nsec;		 
};

/* TIME64_MAX, TIME64_MIN removed - timestamp_truncate gone */

/* KTIME_SEC_MAX removed - never used */

/* timespec64_compare, timespec64_sub, timespec64_valid, timespec64_valid_settod,
   timespec64_to_ns, set_normalized_timespec64, timespec64_add_ns removed - unused */

/* ns_to_timespec64 removed - never called */

#endif  
