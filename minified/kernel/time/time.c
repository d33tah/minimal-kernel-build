// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  This file contains the interface functions for the various time related
 *  system calls: time, stime, gettimeofday, settimeofday, adjtime
 *
 * Modification history:
 *
 * 1993-09-02    Philip Gladstone
 *      Created file with time related functions from sched/core.c and adjtimex()
 * 1993-10-08    Torsten Duwe
 *      adjtime interface update and CMOS clock write code
 * 1995-08-13    Torsten Duwe
 *      kernel PLL updated to 1994-12-13 specs (rfc-1589)
 * 1999-01-16    Ulrich Windl
 *	Introduced error checking for many cases in adjtimex().
 *	Updated NTP code according to technical memorandum Jan '96
 *	"A Kernel Model for Precision Timekeeping" by Dave Mills
 *	Allow time_constant larger than MAXTC(6) for NTP v4 (MAXTC == 10)
 *	(Even though the technical memorandum forbids it)
 * 2004-07-14	 Christoph Lameter
 *	Added getnstimeofday to allow the posix timer functions to return
 *	with nanosecond accuracy
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/timex.h>
#include <linux/capability.h>
#include <linux/timekeeper_internal.h>
#include <linux/errno.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/fs.h>
#include <linux/math64.h>
#include <linux/ptrace.h>

#include <linux/uaccess.h>
#include <linux/compat.h>
#include <asm/unistd.h>

#include <generated/timeconst.h>
#include "timekeeping.h"

/*
 * The timezone where the local system is located.  Used as a default by some
 * programs who obtain this value by using gettimeofday.
 */
struct timezone sys_tz;

EXPORT_SYMBOL(sys_tz);



SYSCALL_DEFINE2(gettimeofday, struct __kernel_old_timeval __user *, tv,
		struct timezone __user *, tz)
{
	if (likely(tv != NULL)) {
		struct timespec64 ts;

		ktime_get_real_ts64(&ts);
		if (put_user(ts.tv_sec, &tv->tv_sec) ||
		    put_user(ts.tv_nsec / 1000, &tv->tv_usec))
			return -EFAULT;
	}
	if (unlikely(tz != NULL)) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}
	return 0;
}

/*
 * In case for some reason the CMOS clock has not already been running
 * in UTC, but in some local time: The first time we set the timezone,
 * we will warp the clock so that it is ticking UTC time instead of
 * local time. Presumably, if someone is setting the timezone then we
 * are running in an environment where the programs understand about
 * timezones. This should be done at boot time in the /etc/rc script,
 * as soon as possible, so that the clock can be set right. Otherwise,
 * various programs will get confused when the clock gets warped.
 */

int do_sys_settimeofday64(const struct timespec64 *tv, const struct timezone *tz)
{
	static int firsttime = 1;
	int error = 0;

	if (tv && !timespec64_valid_settod(tv))
		return -EINVAL;

	error = security_settime64(tv, tz);
	if (error)
		return error;

	if (tz) {
		/* Verify we're within the +-15 hrs range */
		if (tz->tz_minuteswest > 15*60 || tz->tz_minuteswest < -15*60)
			return -EINVAL;

		sys_tz = *tz;
		update_vsyscall_tz();
		if (firsttime) {
			firsttime = 0;
			if (!tv)
				timekeeping_warp_clock();
		}
	}
	if (tv)
		return do_settimeofday64(tv);
	return 0;
}

SYSCALL_DEFINE2(settimeofday, struct __kernel_old_timeval __user *, tv,
		struct timezone __user *, tz)
{
	struct timespec64 new_ts;
	struct timezone new_tz;

	if (tv) {
		if (get_user(new_ts.tv_sec, &tv->tv_sec) ||
		    get_user(new_ts.tv_nsec, &tv->tv_usec))
			return -EFAULT;

		if (new_ts.tv_nsec > USEC_PER_SEC || new_ts.tv_nsec < 0)
			return -EINVAL;

		new_ts.tv_nsec *= NSEC_PER_USEC;
	}
	if (tz) {
		if (copy_from_user(&new_tz, tz, sizeof(*tz)))
			return -EFAULT;
	}

	return do_sys_settimeofday64(tv ? &new_ts : NULL, tz ? &new_tz : NULL);
}




/*
 * Convert jiffies to milliseconds and back.
 *
 * Avoid unnecessary multiplications/divisions in the
 * two most common HZ cases:
 */
unsigned int jiffies_to_msecs(const unsigned long j)
{
	return (MSEC_PER_SEC / HZ) * j;
}
EXPORT_SYMBOL(jiffies_to_msecs);

unsigned int jiffies_to_usecs(const unsigned long j)
{
	/*
	 * Hz usually doesn't go much further MSEC_PER_SEC.
	 * jiffies_to_usecs() and usecs_to_jiffies() depend on that.
	 */
	BUILD_BUG_ON(HZ > USEC_PER_SEC);

	return (USEC_PER_SEC / HZ) * j;
}
EXPORT_SYMBOL(jiffies_to_usecs);

/*
 * mktime64 - Converts date to seconds.
 * Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * A leap second can be indicated by calling this function with sec as
 * 60 (allowable under ISO 8601).  The leap second is treated the same
 * as the following second since they don't exist in UNIX time.
 *
 * An encoding of midnight at the end of the day as 24:00:00 - ie. midnight
 * tomorrow - (allowable under ISO 8601) is supported.
 */
time64_t mktime64(const unsigned int year0, const unsigned int mon0,
		const unsigned int day, const unsigned int hour,
		const unsigned int min, const unsigned int sec)
{
	unsigned int mon = mon0, year = year0;

	/* 1..12 -> 11,12,1..10 */
	if (0 >= (int) (mon -= 2)) {
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}

	return ((((time64_t)
		  (year/4 - year/100 + year/400 + 367*mon/12 + day) +
		  year*365 - 719499
	    )*24 + hour /* now have hours - midnight tomorrow handled here */
	  )*60 + min /* now have minutes */
	)*60 + sec; /* finally seconds */
}
EXPORT_SYMBOL(mktime64);

struct __kernel_old_timeval ns_to_kernel_old_timeval(const s64 nsec)
{
	struct timespec64 ts = ns_to_timespec64(nsec);
	struct __kernel_old_timeval tv;

	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = (suseconds_t)ts.tv_nsec / 1000;

	return tv;
}
EXPORT_SYMBOL(ns_to_kernel_old_timeval);

/**
 * set_normalized_timespec - set timespec sec and nsec parts and normalize
 *
 * @ts:		pointer to timespec variable to be set
 * @sec:	seconds to set
 * @nsec:	nanoseconds to set
 *
 * Set seconds and nanoseconds field of a timespec variable and
 * normalize to the timespec storage format
 *
 * Note: The tv_nsec part is always in the range of
 *	0 <= tv_nsec < NSEC_PER_SEC
 * For negative values only the tv_sec field is negative !
 */
void set_normalized_timespec64(struct timespec64 *ts, time64_t sec, s64 nsec)
{
	while (nsec >= NSEC_PER_SEC) {
		/*
		 * The following asm() prevents the compiler from
		 * optimising this loop into a modulo operation. See
		 * also __iter_div_u64_rem() in include/linux/time.h
		 */
		asm("" : "+rm"(nsec));
		nsec -= NSEC_PER_SEC;
		++sec;
	}
	while (nsec < 0) {
		asm("" : "+rm"(nsec));
		nsec += NSEC_PER_SEC;
		--sec;
	}
	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}
EXPORT_SYMBOL(set_normalized_timespec64);

/**
 * ns_to_timespec64 - Convert nanoseconds to timespec64
 * @nsec:       the nanoseconds value to be converted
 *
 * Returns the timespec64 representation of the nsec parameter.
 */
struct timespec64 ns_to_timespec64(const s64 nsec)
{
	struct timespec64 ts = { 0, 0 };
	s32 rem;

	if (likely(nsec > 0)) {
		ts.tv_sec = div_u64_rem(nsec, NSEC_PER_SEC, &rem);
		ts.tv_nsec = rem;
	} else if (nsec < 0) {
		/*
		 * With negative times, tv_sec points to the earlier
		 * second, and tv_nsec counts the nanoseconds since
		 * then, so tv_nsec is always a positive number.
		 */
		ts.tv_sec = -div_u64_rem(-nsec - 1, NSEC_PER_SEC, &rem) - 1;
		ts.tv_nsec = NSEC_PER_SEC - rem - 1;
	}

	return ts;
}
EXPORT_SYMBOL(ns_to_timespec64);

/**
 * msecs_to_jiffies: - convert milliseconds to jiffies
 * @m:	time in milliseconds
 *
 * conversion is done as follows:
 *
 * - negative values mean 'infinite timeout' (MAX_JIFFY_OFFSET)
 *
 * - 'too large' values [that would result in larger than
 *   MAX_JIFFY_OFFSET values] mean 'infinite timeout' too.
 *
 * - all other values are converted to jiffies by either multiplying
 *   the input value by a factor or dividing it with a factor and
 *   handling any 32-bit overflows.
 *   for the details see __msecs_to_jiffies()
 *
 * msecs_to_jiffies() checks for the passed in value being a constant
 * via __builtin_constant_p() allowing gcc to eliminate most of the
 * code, __msecs_to_jiffies() is called if the value passed does not
 * allow constant folding and the actual conversion must be done at
 * runtime.
 * the _msecs_to_jiffies helpers are the HZ dependent conversion
 * routines found in include/linux/jiffies.h
 */
unsigned long __msecs_to_jiffies(const unsigned int m)
{
	/*
	 * Negative value, means infinite timeout:
	 */
	if ((int)m < 0)
		return MAX_JIFFY_OFFSET;
	return _msecs_to_jiffies(m);
}
EXPORT_SYMBOL(__msecs_to_jiffies);

unsigned long __usecs_to_jiffies(const unsigned int u)
{
	if (u > jiffies_to_usecs(MAX_JIFFY_OFFSET))
		return MAX_JIFFY_OFFSET;
	return _usecs_to_jiffies(u);
}
EXPORT_SYMBOL(__usecs_to_jiffies);

/*
 * The TICK_NSEC - 1 rounds up the value to the next resolution.  Note
 * that a remainder subtract here would not do the right thing as the
 * resolution values don't fall on second boundaries.  I.e. the line:
 * nsec -= nsec % TICK_NSEC; is NOT a correct resolution rounding.
 * Note that due to the small error in the multiplier here, this
 * rounding is incorrect for sufficiently large values of tv_nsec, but
 * well formed timespecs should have tv_nsec < NSEC_PER_SEC, so we're
 * OK.
 *
 * Rather, we just shift the bits off the right.
 *
 * The >> (NSEC_JIFFIE_SC - SEC_JIFFIE_SC) converts the scaled nsec
 * value to a scaled second value.
 */

unsigned long
timespec64_to_jiffies(const struct timespec64 *value)
{
	u64 sec = value->tv_sec;
	long nsec = value->tv_nsec + TICK_NSEC - 1;

	if (sec >= MAX_SEC_IN_JIFFIES){
		sec = MAX_SEC_IN_JIFFIES;
		nsec = 0;
	}
	return ((sec * SEC_CONVERSION) +
		(((u64)nsec * NSEC_CONVERSION) >>
		 (NSEC_JIFFIE_SC - SEC_JIFFIE_SC))) >> SEC_JIFFIE_SC;

}
EXPORT_SYMBOL(timespec64_to_jiffies);

void
jiffies_to_timespec64(const unsigned long jiffies, struct timespec64 *value)
{
	/*
	 * Convert jiffies to nanoseconds and separate with
	 * one divide.
	 */
	u32 rem;
	value->tv_sec = div_u64_rem((u64)jiffies * TICK_NSEC,
				    NSEC_PER_SEC, &rem);
	value->tv_nsec = rem;
}
EXPORT_SYMBOL(jiffies_to_timespec64);

/*
 * Convert jiffies/jiffies_64 to clock_t and back.
 */
clock_t jiffies_to_clock_t(unsigned long x)
{
	return div_u64((u64)x * TICK_NSEC, NSEC_PER_SEC / USER_HZ);
}
EXPORT_SYMBOL(jiffies_to_clock_t);

unsigned long clock_t_to_jiffies(unsigned long x)
{
	/* Don't worry about loss of precision here .. */
	if (x >= ~0UL / HZ * USER_HZ)
		return ~0UL;

	/* .. but do try to contain it here */
	return div_u64((u64)x * HZ, USER_HZ);
}
EXPORT_SYMBOL(clock_t_to_jiffies);

u64 jiffies_64_to_clock_t(u64 x)
{
	/*
	 * There are better ways that don't overflow early,
	 * but even this doesn't overflow in hundreds of years
	 * in 64 bits, so..
	 */
	x = div_u64(x * TICK_NSEC, (NSEC_PER_SEC / USER_HZ));
	return x;
}
EXPORT_SYMBOL(jiffies_64_to_clock_t);

u64 nsec_to_clock_t(u64 x)
{
	return div_u64(x, NSEC_PER_SEC / USER_HZ);
}

u64 jiffies64_to_nsecs(u64 j)
{
	return (NSEC_PER_SEC / HZ) * j;
}
EXPORT_SYMBOL(jiffies64_to_nsecs);

u64 jiffies64_to_msecs(const u64 j)
{
	return (MSEC_PER_SEC / HZ) * j;
}
EXPORT_SYMBOL(jiffies64_to_msecs);

/**
 * nsecs_to_jiffies64 - Convert nsecs in u64 to jiffies64
 *
 * @n:	nsecs in u64
 *
 * Unlike {m,u}secs_to_jiffies, type of input is not unsigned int but u64.
 * And this doesn't return MAX_JIFFY_OFFSET since this function is designed
 * for scheduler, not for use in device drivers to calculate timeout value.
 *
 * note:
 *   NSEC_PER_SEC = 10^9 = (5^9 * 2^9) = (1953125 * 512)
 *   ULLONG_MAX ns = 18446744073.709551615 secs = about 584 years
 */
u64 nsecs_to_jiffies64(u64 n)
{
	/* Common case, HZ = 100, 128, 200, 250, 256, 500, 512, 1000 etc. */
	return div_u64(n, NSEC_PER_SEC / HZ);
}
EXPORT_SYMBOL(nsecs_to_jiffies64);

/**
 * nsecs_to_jiffies - Convert nsecs in u64 to jiffies
 *
 * @n:	nsecs in u64
 *
 * Unlike {m,u}secs_to_jiffies, type of input is not unsigned int but u64.
 * And this doesn't return MAX_JIFFY_OFFSET since this function is designed
 * for scheduler, not for use in device drivers to calculate timeout value.
 *
 * note:
 *   NSEC_PER_SEC = 10^9 = (5^9 * 2^9) = (1953125 * 512)
 *   ULLONG_MAX ns = 18446744073.709551615 secs = about 584 years
 */
unsigned long nsecs_to_jiffies(u64 n)
{
	return (unsigned long)nsecs_to_jiffies64(n);
}
EXPORT_SYMBOL_GPL(nsecs_to_jiffies);

/*
 * Add two timespec64 values and do a safety check for overflow.
 * It's assumed that both values are valid (>= 0).
 * And, each timespec64 is in normalized form.
 */
struct timespec64 timespec64_add_safe(const struct timespec64 lhs,
				const struct timespec64 rhs)
{
	struct timespec64 res;

	set_normalized_timespec64(&res, (timeu64_t) lhs.tv_sec + rhs.tv_sec,
			lhs.tv_nsec + rhs.tv_nsec);

	if (unlikely(res.tv_sec < lhs.tv_sec || res.tv_sec < rhs.tv_sec)) {
		res.tv_sec = TIME64_MAX;
		res.tv_nsec = 0;
	}

	return res;
}

int get_timespec64(struct timespec64 *ts,
		   const struct __kernel_timespec __user *uts)
{
	struct __kernel_timespec kts;
	int ret;

	ret = copy_from_user(&kts, uts, sizeof(kts));
	if (ret)
		return -EFAULT;

	ts->tv_sec = kts.tv_sec;

	/* Zero out the padding in compat mode */
	if (in_compat_syscall())
		kts.tv_nsec &= 0xFFFFFFFFUL;

	/* In 32-bit mode, this drops the padding */
	ts->tv_nsec = kts.tv_nsec;

	return 0;
}
EXPORT_SYMBOL_GPL(get_timespec64);

int put_timespec64(const struct timespec64 *ts,
		   struct __kernel_timespec __user *uts)
{
	struct __kernel_timespec kts = {
		.tv_sec = ts->tv_sec,
		.tv_nsec = ts->tv_nsec
	};

	return copy_to_user(uts, &kts, sizeof(kts)) ? -EFAULT : 0;
}
EXPORT_SYMBOL_GPL(put_timespec64);

static int __get_old_timespec32(struct timespec64 *ts64,
				   const struct old_timespec32 __user *cts)
{
	struct old_timespec32 ts;
	int ret;

	ret = copy_from_user(&ts, cts, sizeof(ts));
	if (ret)
		return -EFAULT;

	ts64->tv_sec = ts.tv_sec;
	ts64->tv_nsec = ts.tv_nsec;

	return 0;
}

static int __put_old_timespec32(const struct timespec64 *ts64,
				   struct old_timespec32 __user *cts)
{
	struct old_timespec32 ts = {
		.tv_sec = ts64->tv_sec,
		.tv_nsec = ts64->tv_nsec
	};
	return copy_to_user(cts, &ts, sizeof(ts)) ? -EFAULT : 0;
}

int get_old_timespec32(struct timespec64 *ts, const void __user *uts)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_from_user(ts, uts, sizeof(*ts)) ? -EFAULT : 0;
	else
		return __get_old_timespec32(ts, uts);
}
EXPORT_SYMBOL_GPL(get_old_timespec32);

int put_old_timespec32(const struct timespec64 *ts, void __user *uts)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_to_user(uts, ts, sizeof(*ts)) ? -EFAULT : 0;
	else
		return __put_old_timespec32(ts, uts);
}
EXPORT_SYMBOL_GPL(put_old_timespec32);

int get_itimerspec64(struct itimerspec64 *it,
			const struct __kernel_itimerspec __user *uit)
{
	int ret;

	ret = get_timespec64(&it->it_interval, &uit->it_interval);
	if (ret)
		return ret;

	ret = get_timespec64(&it->it_value, &uit->it_value);

	return ret;
}
EXPORT_SYMBOL_GPL(get_itimerspec64);

int put_itimerspec64(const struct itimerspec64 *it,
			struct __kernel_itimerspec __user *uit)
{
	int ret;

	ret = put_timespec64(&it->it_interval, &uit->it_interval);
	if (ret)
		return ret;

	ret = put_timespec64(&it->it_value, &uit->it_value);

	return ret;
}
EXPORT_SYMBOL_GPL(put_itimerspec64);

int get_old_itimerspec32(struct itimerspec64 *its,
			const struct old_itimerspec32 __user *uits)
{

	if (__get_old_timespec32(&its->it_interval, &uits->it_interval) ||
	    __get_old_timespec32(&its->it_value, &uits->it_value))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(get_old_itimerspec32);

int put_old_itimerspec32(const struct itimerspec64 *its,
			struct old_itimerspec32 __user *uits)
{
	if (__put_old_timespec32(&its->it_interval, &uits->it_interval) ||
	    __put_old_timespec32(&its->it_value, &uits->it_value))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(put_old_itimerspec32);
