#ifndef _LINUX_TIME64_H
#define _LINUX_TIME64_H

#include <linux/math64.h>

#define NSEC_PER_USEC	1000L
#define NSEC_PER_MSEC	1000000L
#define NSEC_PER_SEC	1000000000L

typedef __s64 time64_t;

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
struct timezone {
	int	tz_minuteswest;
	int	tz_dsttime;
};

struct timespec64 {
	time64_t	tv_sec;			 
	long		tv_nsec;		 
};

#endif  
