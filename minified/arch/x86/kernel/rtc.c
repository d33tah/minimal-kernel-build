#include <asm/io.h>
#include <asm/processor.h>
#include <linux/smp.h>
#include <linux/time64.h> /* for struct timespec64 */
#include <linux/delay.h>
#include <linux/spinlock.h>
extern spinlock_t rtc_lock;
#define RTC_PORT(x) (0x70 + (x))
#define RTC_SECONDS 0
#define RTC_MINUTES 2
#define RTC_HOURS 4
#define RTC_DAY_OF_MONTH 7
#define RTC_MONTH 8
#define RTC_YEAR 9
#define RTC_FREQ_SELECT 10
#define RTC_UIP 0x80
#define CMOS_READ(addr) rtc_cmos_read(addr)
unsigned char rtc_cmos_read(unsigned char addr);
volatile unsigned long cmos_lock;
static inline void lock_cmos(unsigned char reg)
{
	unsigned long new;
	new = ((smp_processor_id() + 1) << 8) | reg;
	for (;;) {
		if (cmos_lock) {
			cpu_relax();
			continue;
		}
		if (__cmpxchg(&cmos_lock, 0, new, sizeof(cmos_lock)) == 0)
			return;
	}
}
static inline void unlock_cmos(void)
{
	cmos_lock = 0;
}
#define lock_cmos_prefix(reg)               \
	do {                                \
		unsigned long cmos_flags;   \
		local_irq_save(cmos_flags); \
	lock_cmos(reg)
#define lock_cmos_suffix(reg)          \
	unlock_cmos();                 \
	local_irq_restore(cmos_flags); \
	}                              \
	while (0)
/* end mc146818rtc.h */
#define bcd2bin(x) \
	(__builtin_constant_p((u8)(x)) ? const_bcd2bin(x) : _bcd2bin(x))
#define const_bcd2bin(x) (((x) & 0x0f) + ((x) >> 4) * 10)
static unsigned _bcd2bin(unsigned char val)
{
	return (val & 0x0f) + (val >> 4) * 10;
}

#include <linux/of.h>

#include <asm/x86_init.h>
#include <asm/time.h>
#include <asm/setup.h>

#define CMOS_YEARS_OFFS 2000

DEFINE_SPINLOCK(rtc_lock);

void mach_get_cmos_time(struct timespec64 *now)
{
	unsigned int year, mon, day, hour, min, sec;
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

	spin_unlock_irqrestore(&rtc_lock, flags);

	sec = bcd2bin(sec);
	min = bcd2bin(min);
	hour = bcd2bin(hour);
	day = bcd2bin(day);
	mon = bcd2bin(mon);
	year = bcd2bin(year);

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
