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

/* Only keep clock IDs actually used */
#define CLOCK_REALTIME			0
#define CLOCK_MONOTONIC			1
#define CLOCK_MONOTONIC_RAW		4
#define CLOCK_REALTIME_COARSE		5
#define CLOCK_MONOTONIC_COARSE		6
#define CLOCK_BOOTTIME			7
#define CLOCK_TAI			11

#define MAX_CLOCKS			16

#endif
