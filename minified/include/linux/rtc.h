#ifndef _LINUX_RTC_H_
#define _LINUX_RTC_H_

/* Minimal rtc.h - only what's needed for time functions */

#include <linux/types.h>
#include <linux/ktime.h>
#include <uapi/linux/rtc.h>

extern int rtc_month_days(unsigned int month, unsigned int year);
extern int rtc_year_days(unsigned int day, unsigned int month, unsigned int year);
extern int rtc_valid_tm(struct rtc_time *tm);
extern time64_t rtc_tm_to_time64(struct rtc_time *tm);
extern void rtc_time64_to_tm(time64_t time, struct rtc_time *tm);
ktime_t rtc_tm_to_ktime(struct rtc_time tm);
struct rtc_time rtc_ktime_to_tm(ktime_t kt);

static inline time64_t rtc_tm_sub(struct rtc_time *lhs, struct rtc_time *rhs)
{
	return rtc_tm_to_time64(lhs) - rtc_tm_to_time64(rhs);
}

static inline bool is_leap_year(unsigned int year)
{
	return (!(year % 4) && (year % 100)) || !(year % 400);
}

#define rtc_hctosys_ret -ENODEV

#endif
