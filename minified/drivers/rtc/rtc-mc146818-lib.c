#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/mc146818rtc.h>


bool mc146818_avoid_UIP(void (*callback)(unsigned char seconds, void *param),
			void *param)
{
	int i;
	unsigned long flags;
	unsigned char seconds;

	for (i = 0; i < 10; i++) {
		spin_lock_irqsave(&rtc_lock, flags);

		 
		seconds = CMOS_READ(RTC_SECONDS);

		if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP) {
			spin_unlock_irqrestore(&rtc_lock, flags);
			mdelay(1);
			continue;
		}

		 
		if (seconds != CMOS_READ(RTC_SECONDS)) {
			spin_unlock_irqrestore(&rtc_lock, flags);
			continue;
		}

		if (callback)
			callback(seconds, param);

		 
		if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP) {
			spin_unlock_irqrestore(&rtc_lock, flags);
			mdelay(1);
			continue;
		}

		 
		if (seconds != CMOS_READ(RTC_SECONDS)) {
			spin_unlock_irqrestore(&rtc_lock, flags);
			continue;
		}
		spin_unlock_irqrestore(&rtc_lock, flags);

		return true;
	}
	return false;
}

/* mc146818_does_rtc_work removed - unused */

struct mc146818_get_time_callback_param {
	struct rtc_time *time;
	unsigned char ctrl;
};

static void mc146818_get_time_callback(unsigned char seconds, void *param_in)
{
	struct mc146818_get_time_callback_param *p = param_in;

	 
	p->time->tm_sec = seconds;
	p->time->tm_min = CMOS_READ(RTC_MINUTES);
	p->time->tm_hour = CMOS_READ(RTC_HOURS);
	p->time->tm_mday = CMOS_READ(RTC_DAY_OF_MONTH);
	p->time->tm_mon = CMOS_READ(RTC_MONTH);
	p->time->tm_year = CMOS_READ(RTC_YEAR);

	p->ctrl = CMOS_READ(RTC_CONTROL);
}

int mc146818_get_time(struct rtc_time *time)
{
	struct mc146818_get_time_callback_param p = {
		.time = time
	};

	if (!mc146818_avoid_UIP(mc146818_get_time_callback, &p)) {
		memset(time, 0, sizeof(*time));
		return -EIO;
	}

	if (!(p.ctrl & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	{
		time->tm_sec = bcd2bin(time->tm_sec);
		time->tm_min = bcd2bin(time->tm_min);
		time->tm_hour = bcd2bin(time->tm_hour);
		time->tm_mday = bcd2bin(time->tm_mday);
		time->tm_mon = bcd2bin(time->tm_mon);
		time->tm_year = bcd2bin(time->tm_year);
	}



	 
	if (time->tm_year <= 69)
		time->tm_year += 100;

	time->tm_mon--;

	return 0;
}

/* mc146818_set_time stubbed - mach_set_rtc_mmss (only caller) is now a stub */
int mc146818_set_time(struct rtc_time *time) { return -EINVAL; }
