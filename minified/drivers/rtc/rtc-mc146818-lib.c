/* Stub: mc146818 functions not called in minimal kernel
 * arch/x86/kernel/rtc.c has mach_get_cmos_time which is used instead */
#include <linux/mc146818rtc.h>
#include <linux/rtc.h>
#include <linux/string.h>

bool mc146818_avoid_UIP(void (*callback)(unsigned char seconds, void *param),
			void *param)
{
	return false;
}
int mc146818_get_time(struct rtc_time *time)
{
	memset(time, 0, sizeof(*time));
	return -EIO;
}
int mc146818_set_time(struct rtc_time *time)
{
	return -EINVAL;
}
