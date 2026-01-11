#include <linux/platform_device.h>
#include <linux/mc146818rtc.h>
/* Inlined from linux/bcd.h */
#define bcd2bin(x) \
	(__builtin_constant_p((u8)(x)) ? const_bcd2bin(x) : _bcd2bin(x))
#define const_bcd2bin(x) (((x) & 0x0f) + ((x) >> 4) * 10)
static unsigned _bcd2bin(unsigned char val)
{
	return (val & 0x0f) + (val >> 4) * 10;
}
#include <linux/export.h>

#include <linux/of.h>

/* asm/vsyscall.h removed - empty */
#include <asm/x86_init.h>
#include <asm/time.h>
#include <asm/setup.h>

volatile unsigned long cmos_lock;

#define CMOS_YEARS_OFFS 2000

DEFINE_SPINLOCK(rtc_lock);

/* mach_set_rtc_mmss removed - x86_platform.set_wallclock never called */

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

/* rtc_cmos_write removed - mc146818_set_time (only caller) is now a stub */

void read_persistent_clock64(struct timespec64 *ts)
{
	x86_platform.get_wallclock(ts);
}

/* add_rtc_cmos removed - was empty stub initcall */
