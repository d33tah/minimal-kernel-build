#include <linux/clocksource.h>

#ifndef _LINUX_TIMEKEEPER_INTERNAL_H
#define _LINUX_TIMEKEEPER_INTERNAL_H

struct tk_read_base {
	struct clocksource *clock;
	u64 mask;
	u64 cycle_last;
	u32 mult;
	u32 shift;
	u64 xtime_nsec;
	ktime_t base;
	u64 base_real;
};

struct timekeeper {
	struct tk_read_base tkr_mono;
	struct tk_read_base tkr_raw;
	u64 xtime_sec;
	unsigned long ktime_sec;
	struct timespec64 wall_to_monotonic;
	ktime_t offs_real;
	ktime_t offs_boot;
	ktime_t offs_tai;
	s32 tai_offset;
	unsigned int clock_was_set_seq;
	u8 cs_was_changed_seq;
	ktime_t next_leap_ktime;
	u64 raw_sec;
	struct timespec64 monotonic_to_boot;

	u64 cycle_interval;
	u64 xtime_interval;
	s64 xtime_remainder;
	u64 raw_interval;
	u64 ntp_tick;
	s64 ntp_error;
	u32 ntp_error_shift;
	u32 ntp_err_mult;

	u32 skip_second_overflow;
};

#endif /* _LINUX_TIMEKEEPER_INTERNAL_H */
#include <linux/syscalls.h>

extern raw_spinlock_t jiffies_lock;
extern seqcount_raw_spinlock_t jiffies_seq;

time64_t mktime64(const unsigned int year0, const unsigned int mon0,
		  const unsigned int day, const unsigned int hour,
		  const unsigned int min, const unsigned int sec)
{
	unsigned int mon = mon0, year = year0;

	if (0 >= (int)(mon -= 2)) {
		mon += 12;
		year -= 1;
	}

	return ((((time64_t)(year / 4 - year / 100 + year / 400 +
			     367 * mon / 12 + day) +
		  year * 365 - 719499) *
			 24 +
		 hour) * 60 +
		min) * 60 +
	       sec;
}
