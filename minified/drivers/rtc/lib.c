/* Stub RTC library - functions never called in minimal kernel */
#include <linux/rtc.h>
int rtc_month_days(unsigned int month, unsigned int year)
{
	return 0;
}
void rtc_time64_to_tm(time64_t time, struct rtc_time *tm)
{
}
int rtc_valid_tm(struct rtc_time *tm)
{
	return 0;
}
