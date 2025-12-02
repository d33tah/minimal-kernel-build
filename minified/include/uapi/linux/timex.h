/* Minimal timex.h - keep only what's actually used */
#ifndef _UAPI_LINUX_TIMEX_H
#define _UAPI_LINUX_TIMEX_H

#include <linux/time.h>

struct __kernel_timex_timeval {
	__kernel_time64_t       tv_sec;
	long long		tv_usec;
};

struct __kernel_timex {
	unsigned int modes;
	int :32;
	long long offset;
	long long freq;
	long long maxerror;
	long long esterror;
	int status;
	int :32;
	long long constant;
	long long precision;
	long long tolerance;
	struct __kernel_timex_timeval time;
	long long tick;
	long long ppsfreq;
	long long jitter;
	int shift;
	int :32;
	long long stabil;
	long long jitcnt;
	long long calcnt;
	long long errcnt;
	long long stbcnt;
	int tai;
	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32;
};

/* Only TIME_ERROR is used in kernel code */
#define TIME_ERROR	5

#endif
