
#ifndef _LINUX_TIMEKEEPER_INTERNAL_H
#define _LINUX_TIMEKEEPER_INTERNAL_H

#include <linux/clocksource.h>

struct tk_read_base {
	struct clocksource *clock;
	u64 mask, cycle_last;
	u32 mult, shift;
	u64 xtime_nsec;
};

struct timekeeper {
	struct tk_read_base tkr_mono;
	u64 xtime_sec;
	u64 cycle_interval, xtime_interval;
	s64 xtime_remainder;
	u64 ntp_tick;
	s64 ntp_error;
	u32 ntp_error_shift, ntp_err_mult;
	u32 skip_second_overflow;
};

#endif
