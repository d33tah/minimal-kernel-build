#ifndef _LINUX_RTC_H_
#define _LINUX_RTC_H_

/* Minimal rtc.h - only what's needed for time functions */

#include <linux/types.h>
#include <linux/ktime.h>

struct rtc_time {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

/* rtc_month_days, rtc_valid_tm, rtc_time64_to_tm removed - declared but never called */
/* rtc_hctosys_ret removed - never used */

#endif
