#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "../../../../lib/vdso/gettimeofday.c"

extern int __vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz);
extern __kernel_old_time_t __vdso_time(__kernel_old_time_t *t);

int __vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz)
{
	return __cvdso_gettimeofday(tv, tz);
}

__kernel_old_time_t __vdso_time(__kernel_old_time_t *t)
{
	return __cvdso_time(t);
}


extern int __vdso_clock_gettime(clockid_t clock, struct old_timespec32 *ts);
extern int __vdso_clock_getres(clockid_t clock, struct old_timespec32 *res);

int __vdso_clock_gettime(clockid_t clock, struct old_timespec32 *ts)
{
	return __cvdso_clock_gettime32(clock, ts);
}

int __vdso_clock_gettime64(clockid_t clock, struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts);
}

int __vdso_clock_getres(clockid_t clock, struct old_timespec32 *res)
{
	return __cvdso_clock_getres_time32(clock, res);
}
