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

extern int rtc_month_days(unsigned int month, unsigned int year);
extern int rtc_valid_tm(struct rtc_time *tm);
extern void rtc_time64_to_tm(time64_t time, struct rtc_time *tm);

static inline bool is_leap_year(unsigned int year)
{
	return (!(year % 4) && (year % 100)) || !(year % 400);
}

#define rtc_hctosys_ret -ENODEV

#endif
