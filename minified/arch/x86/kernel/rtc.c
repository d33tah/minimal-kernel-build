 
 
#include <linux/platform_device.h>
#include <linux/mc146818rtc.h>
#include <linux/acpi.h>
#include <linux/bcd.h>
#include <linux/export.h>

#include <linux/of.h>

#include <asm/vsyscall.h>
#include <asm/x86_init.h>
#include <asm/time.h>
#include <asm/intel-mid.h>
#include <asm/setup.h>

 
volatile unsigned long cmos_lock;

 
#define CMOS_YEARS_OFFS 2000

DEFINE_SPINLOCK(rtc_lock);

 
int mach_set_rtc_mmss(const struct timespec64 *now)
{
	unsigned long long nowtime = now->tv_sec;
	struct rtc_time tm;
	int retval = 0;

	rtc_time64_to_tm(nowtime, &tm);
	if (!rtc_valid_tm(&tm)) {
		retval = mc146818_set_time(&tm);
		if (retval)
			printk(KERN_ERR "%s: RTC write failed with error %d\n",
			       __func__, retval);
	} else {
		printk(KERN_ERR
		       "%s: Invalid RTC value: write of %llx to RTC failed\n",
			__func__, nowtime);
		retval = -EINVAL;
	}
	return retval;
}

void mach_get_cmos_time(struct timespec64 *now)
{
	unsigned int status, year, mon, day, hour, min, sec, century = 0;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);

	 
	while ((CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP))
		cpu_relax();

	sec = CMOS_READ(RTC_SECONDS);
	min = CMOS_READ(RTC_MINUTES);
	hour = CMOS_READ(RTC_HOURS);
	day = CMOS_READ(RTC_DAY_OF_MONTH);
	mon = CMOS_READ(RTC_MONTH);
	year = CMOS_READ(RTC_YEAR);


	status = CMOS_READ(RTC_CONTROL);
	WARN_ON_ONCE(RTC_ALWAYS_BCD && (status & RTC_DM_BINARY));

	spin_unlock_irqrestore(&rtc_lock, flags);

	if (RTC_ALWAYS_BCD || !(status & RTC_DM_BINARY)) {
		sec = bcd2bin(sec);
		min = bcd2bin(min);
		hour = bcd2bin(hour);
		day = bcd2bin(day);
		mon = bcd2bin(mon);
		year = bcd2bin(year);
	}

	if (century) {
		century = bcd2bin(century);
		year += century * 100;
	} else
		year += CMOS_YEARS_OFFS;

	now->tv_sec = mktime64(year, mon, day, hour, min, sec);
	now->tv_nsec = 0;
}

 
unsigned char rtc_cmos_read(unsigned char addr)
{
	unsigned char val;

	lock_cmos_prefix(addr);
	outb(addr, RTC_PORT(0));
	val = inb(RTC_PORT(1));
	lock_cmos_suffix(addr);

	return val;
}

void rtc_cmos_write(unsigned char val, unsigned char addr)
{
	lock_cmos_prefix(addr);
	outb(addr, RTC_PORT(0));
	outb(val, RTC_PORT(1));
	lock_cmos_suffix(addr);
}

int update_persistent_clock64(struct timespec64 now)
{
	return x86_platform.set_wallclock(&now);
}

 
void read_persistent_clock64(struct timespec64 *ts)
{
	x86_platform.get_wallclock(ts);
}


/* Stub: RTC platform device registration not needed for minimal kernel */
static __init int add_rtc_cmos(void)
{
	return 0;
}
device_initcall(add_rtc_cmos);
