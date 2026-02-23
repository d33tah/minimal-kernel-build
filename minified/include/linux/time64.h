#ifndef _LINUX_TIME64_H
#define _LINUX_TIME64_H

#include <linux/math64.h>

#define NSEC_PER_MSEC	1000000L
#define NSEC_PER_SEC	1000000000L

typedef __s64 time64_t;


struct timespec64 {
	time64_t	tv_sec;			 
	long		tv_nsec;		 
};

#endif  
