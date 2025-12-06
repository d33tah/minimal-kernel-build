/* --- 2025-12-06 13:15 --- time_types.h inlined */
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

struct __kernel_old_timespec {
	__kernel_old_time_t	tv_sec;
	long			tv_nsec;
};

struct __kernel_old_itimerval {
	struct __kernel_old_timeval it_interval;
	struct __kernel_old_timeval it_value;
};

struct __kernel_sock_timeval {
	__s64 tv_sec;
	__s64 tv_usec;
};
/* End time_types.h */

#ifndef __KERNEL__
#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
	__kernel_old_time_t	tv_sec;
	long			tv_nsec;
};
#endif

struct timeval {
	__kernel_old_time_t	tv_sec;
	__kernel_suseconds_t	tv_usec;
};

struct itimerspec {
	struct timespec it_interval;
	struct timespec it_value;
};

struct itimerval {
	struct timeval it_interval;
	struct timeval it_value;
};
#endif

struct timezone {
	int	tz_minuteswest;
	int	tz_dsttime;
};

#define	ITIMER_REAL		0
#define	ITIMER_VIRTUAL		1
#define	ITIMER_PROF		2

#define CLOCK_REALTIME			0
#define CLOCK_MONOTONIC			1
#define CLOCK_PROCESS_CPUTIME_ID	2
#define CLOCK_THREAD_CPUTIME_ID		3
#define CLOCK_MONOTONIC_RAW		4
#define CLOCK_REALTIME_COARSE		5
#define CLOCK_MONOTONIC_COARSE		6
#define CLOCK_BOOTTIME			7
#define CLOCK_REALTIME_ALARM		8
#define CLOCK_BOOTTIME_ALARM		9
#define CLOCK_SGI_CYCLE			10
#define CLOCK_TAI			11

#define MAX_CLOCKS			16
#define CLOCKS_MASK			(CLOCK_REALTIME | CLOCK_MONOTONIC)
#define CLOCKS_MONO			CLOCK_MONOTONIC

#define TIMER_ABSTIME			0x01

#endif
