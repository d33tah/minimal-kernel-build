#ifndef _UAPI_LINUX_TIME_H
#define _UAPI_LINUX_TIME_H

#include <linux/types.h>

/* Inlined from time_types.h */
struct __kernel_timespec {
	__kernel_time64_t       tv_sec;
	long long               tv_nsec;
};

struct __kernel_itimerspec {
	struct __kernel_timespec it_interval;
	struct __kernel_timespec it_value;
};

#ifndef __kernel_old_timeval
struct __kernel_old_timeval {
	__kernel_long_t tv_sec;
	__kernel_long_t tv_usec;
};
#endif

/* __kernel_sock_timeval - unused */
/* End time_types.h */

struct timezone {
	int	tz_minuteswest;
	int	tz_dsttime;
};

/* Only ITIMER_REAL is used */
#define	ITIMER_REAL		0

/* Only keep clock IDs actually used */
#define CLOCK_REALTIME			0
#define CLOCK_MONOTONIC			1
#define CLOCK_MONOTONIC_RAW		4
#define CLOCK_REALTIME_COARSE		5
#define CLOCK_MONOTONIC_COARSE		6
#define CLOCK_BOOTTIME			7
#define CLOCK_TAI			11

#define MAX_CLOCKS			16
#define CLOCKS_MASK			(CLOCK_REALTIME | CLOCK_MONOTONIC)
#define CLOCKS_MONO			CLOCK_MONOTONIC

#define TIMER_ABSTIME			0x01

#endif
