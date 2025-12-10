#ifndef _LINUX_TIME32_H
#define _LINUX_TIME32_H

#include <linux/time64.h>
#include <linux/timex.h>

/* Inlined from vdso/time32.h */
typedef s32		old_time32_t;

struct old_timespec32 {
	old_time32_t	tv_sec;
	s32		tv_nsec;
};

struct old_timeval32 {
	old_time32_t	tv_sec;
	s32		tv_usec;
};

struct old_itimerspec32 {
	struct old_timespec32 it_interval;
	struct old_timespec32 it_value;
};

struct old_utimbuf32 {
	old_time32_t	actime;
	old_time32_t	modtime;
};

struct old_timex32 {
	u32 modes;
	s32 offset;
	s32 freq;
	s32 maxerror;
	s32 esterror;
	s32 status;
	s32 constant;
	s32 precision;
	s32 tolerance;
	struct old_timeval32 time;
	s32 tick;
	s32 ppsfreq;
	s32 jitter;
	s32 shift;
	s32 stabil;
	s32 jitcnt;
	s32 calcnt;
	s32 errcnt;
	s32 stbcnt;
	s32 tai;

	s32:32; s32:32; s32:32; s32:32;
	s32:32; s32:32; s32:32; s32:32;
	s32:32; s32:32; s32:32;
};

/* get_old_timespec32, put_old_timespec32, get_old_itimerspec32, put_old_itimerspec32, get_old_timex32, put_old_timex32 removed - unused */

extern struct __kernel_old_timeval ns_to_kernel_old_timeval(s64 nsec);

#endif
