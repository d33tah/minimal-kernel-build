#ifndef __VDSO_DATAPAGE_H
#define __VDSO_DATAPAGE_H

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <uapi/linux/time.h>
#include <uapi/linux/types.h>
/* linux/errno.h removed - no errno constants used */
#include <linux/bits.h>
#include <linux/limits.h>
#include <linux/clocksource.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/time64.h>
/* time32.h removed - types now in time.h */
#include <linux/time.h>

struct arch_vdso_data {};

#define VDSO_BASES	(CLOCK_TAI + 1)
#define VDSO_HRES	(BIT(CLOCK_REALTIME)		| \
			 BIT(CLOCK_MONOTONIC)		| \
			 BIT(CLOCK_BOOTTIME)		| \
			 BIT(CLOCK_TAI))
#define VDSO_COARSE	(BIT(CLOCK_REALTIME_COARSE)	| \
			 BIT(CLOCK_MONOTONIC_COARSE))
#define VDSO_RAW	(BIT(CLOCK_MONOTONIC_RAW))

#define CS_HRES_COARSE	0
#define CS_RAW		1
#define CS_BASES	(CS_RAW + 1)

struct vdso_timestamp {
	u64	sec;
	u64	nsec;
};

struct vdso_data {
	u32			seq;

	s32			clock_mode;
	u64			cycle_last;
	u64			mask;
	u32			mult;
	u32			shift;

	union {
		struct vdso_timestamp	basetime[VDSO_BASES];
		struct timens_offset	offset[VDSO_BASES];
	};

	s32			tz_minuteswest;
	s32			tz_dsttime;
	u32			hrtimer_res;
	u32			__unused;

	struct arch_vdso_data	arch_data;
};

extern struct vdso_data _vdso_data[CS_BASES] __attribute__((visibility("hidden")));

/* gettimeofday.h inlined - single includer */
#include <uapi/linux/time.h>
#include <asm/vgtod.h>
#include <asm/vvar.h>
#include <asm/unistd.h>

/* Hardcoded syscall numbers for vDSO fallbacks - syscall table reduced */
#ifndef __NR_gettimeofday
#define __NR_gettimeofday 78
#endif
#ifndef __NR_clock_gettime
#define __NR_clock_gettime 265
#endif
#ifndef __NR_clock_getres
#define __NR_clock_getres 266
#endif
#ifndef __NR_clock_gettime64
#define __NR_clock_gettime64 403
#endif
/* __NR_clock_getres_time64 removed - clock_getres_fallback removed */
#include <asm/msr.h>
/* pvclock.h removed - header is empty */
#define __vdso_data (VVAR(_vdso_data))
/* __timens_vdso_data removed - never used */

#define VDSO_HAS_TIME 1

#define VDSO_HAS_CLOCK_GETRES 1




#ifndef BUILD_VDSO32

static __always_inline
long clock_gettime_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	long ret;

	asm ("syscall" : "=a" (ret), "=m" (*_ts) :
	     "0" (__NR_clock_gettime), "D" (_clkid), "S" (_ts) :
	     "rcx", "r11");

	return ret;
}

static __always_inline
long gettimeofday_fallback(struct __kernel_old_timeval *_tv,
			   struct timezone *_tz)
{
	long ret;

	asm("syscall" : "=a" (ret) :
	    "0" (__NR_gettimeofday), "D" (_tv), "S" (_tz) : "memory");

	return ret;
}

/* clock_getres_fallback removed - never called */

#else

static __always_inline
long clock_gettime_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	long ret;

	asm (
		"mov %%ebx, %%edx \n"
		"mov %[clock], %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret), "=m" (*_ts)
		: "0" (__NR_clock_gettime64), [clock] "g" (_clkid), "c" (_ts)
		: "edx");

	return ret;
}

static __always_inline
long clock_gettime32_fallback(clockid_t _clkid, struct old_timespec32 *_ts)
{
	long ret;

	asm (
		"mov %%ebx, %%edx \n"
		"mov %[clock], %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret), "=m" (*_ts)
		: "0" (__NR_clock_gettime), [clock] "g" (_clkid), "c" (_ts)
		: "edx");

	return ret;
}

static __always_inline
long gettimeofday_fallback(struct __kernel_old_timeval *_tv,
			   struct timezone *_tz)
{
	long ret;

	asm(
		"mov %%ebx, %%edx \n"
		"mov %2, %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret)
		: "0" (__NR_gettimeofday), "g" (_tv), "c" (_tz)
		: "memory", "edx");

	return ret;
}

/* clock_getres_fallback removed - never called */

static __always_inline
long clock_getres32_fallback(clockid_t _clkid, struct old_timespec32 *_ts)
{
	long ret;

	asm (
		"mov %%ebx, %%edx \n"
		"mov %[clock], %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret), "=m" (*_ts)
		: "0" (__NR_clock_getres), [clock] "g" (_clkid), "c" (_ts)
		: "edx");

	return ret;
}

#endif

/* __arch_get_hw_counter inlined at lib/vdso/gettimeofday.c - single caller */

static __always_inline const struct vdso_data *__arch_get_vdso_data(void)
{
	return __vdso_data;
}

static inline bool arch_vdso_clocksource_ok(const struct vdso_data *vd)
{
	return true;
}
#define vdso_clocksource_ok arch_vdso_clocksource_ok


static inline bool arch_vdso_cycles_ok(u64 cycles)
{
	return (s64)cycles >= 0;
}
#define vdso_cycles_ok arch_vdso_cycles_ok


static __always_inline
u64 vdso_calc_delta(u64 cycles, u64 last, u64 mask, u32 mult)
{
	if (cycles > last)
		return (cycles - last) * mult;
	return 0;
}
#define vdso_calc_delta vdso_calc_delta

#endif

#endif  
