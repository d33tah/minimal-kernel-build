#ifndef _UAPI_LINUX_TIME_H
#define _UAPI_LINUX_TIME_H

#include <linux/types.h>

struct __kernel_timespec {
	__kernel_time64_t       tv_sec;
	long long               tv_nsec;
};

#ifndef __kernel_old_timeval
struct __kernel_old_timeval {
	__kernel_long_t tv_sec;
	__kernel_long_t tv_usec;
};
#endif

/* End time_types.h */

struct timezone {
	int	tz_minuteswest;
	int	tz_dsttime;
};

#define CLOCK_TAI			11

#endif
