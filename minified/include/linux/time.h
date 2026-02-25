#ifndef _LINUX_TIME_H
#define _LINUX_TIME_H
#include <linux/cache.h>
#include <linux/math64.h>
/* time64.h inlined */
#include <linux/math64.h>
#ifndef NSEC_PER_SEC
#define NSEC_PER_MSEC	1000000L
#define NSEC_PER_SEC	1000000000L
typedef __s64 time64_t;
struct timespec64 {
	time64_t	tv_sec;
	long		tv_nsec;
};
#endif
#endif
