
#include <linux/delay.h>
#include <linux/i8253.h>
#include <linux/jiffies.h>

#include <asm/div64.h>
#include <asm/io.h>
#include <asm/i8259.h>
#include <asm/tsc.h>

static unsigned int __read_mostly cpu_khz;

static unsigned int __read_mostly tsc_khz;

#define KHZ 1000

static inline int pit_verify_msb(unsigned char val)
{
	inb(0x42);
	return inb(0x42) == val;
}

static inline int pit_expect_msb(unsigned char val, u64 *tscp,
				 unsigned long *deltap)
{
	int count;
	u64 tsc = 0, prev_tsc = 0;

	for (count = 0; count < 50000; count++) {
		if (!pit_verify_msb(val))
			break;
		prev_tsc = tsc;
		tsc = get_cycles();
	}
	*deltap = get_cycles() - prev_tsc;
	*tscp = tsc;

	return count > 5;
}

#define MAX_QUICK_PIT_MS 50
#define MAX_QUICK_PIT_ITERATIONS (MAX_QUICK_PIT_MS * PIT_TICK_RATE / 1000 / 256)

static unsigned long quick_pit_calibrate(void)
{
	int i;
	u64 tsc, delta;
	unsigned long d1, d2;

	if (!has_legacy_pic())
		return 0;

	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	outb(0xb0, 0x43);

	outb(0xff, 0x42);
	outb(0xff, 0x42);

	pit_verify_msb(0);

	if (pit_expect_msb(0xff, &tsc, &d1)) {
		for (i = 1; i <= MAX_QUICK_PIT_ITERATIONS; i++) {
			if (!pit_expect_msb(0xff - i, &delta, &d2))
				break;

			delta -= tsc;

			if (i == 1 &&
			    d1 + d2 >= (delta * MAX_QUICK_PIT_ITERATIONS) >> 11)
				return 0;

			if (d1 + d2 >= delta >> 11)
				continue;

			if (!pit_verify_msb(0xfe - i))
				break;
			goto success;
		}
	}
	return 0;

success:

	delta *= PIT_TICK_RATE;
	do_div(delta, i * 256 * 1000);
	return delta;
}

/*
 * The CPU (and TSC) frequency is measured against the PIT; there is no
 * vendor-specific CPUID 0x15/0x16 frequency enumeration.
 */
static unsigned long native_calibrate_cpu_early(void)
{
	unsigned long flags, fast_calibrate;

	local_irq_save(flags);
	fast_calibrate = quick_pit_calibrate();
	local_irq_restore(flags);
	return fast_calibrate;
}

static bool __init determine_cpu_tsc_frequencies(void)
{
	/*
	 * Only the early quick-PIT calibration (native_calibrate_cpu_early())
	 * survives on this build; the late re-calibration is gone. When it
	 * fails, tsc_early_init() and tsc_init() return early and the
	 * jiffies-calibrated delay loop stays in use.
	 */
	cpu_khz = native_calibrate_cpu_early();
	tsc_khz = cpu_khz;

	if (tsc_khz == 0)
		return false;

	return true;
}

static unsigned long __init get_loops_per_jiffy(void)
{
	u64 lpj = (u64)tsc_khz * KHZ;

	do_div(lpj, HZ);
	return lpj;
}

static void __init tsc_enable_sched_clock(void)
{
	loops_per_jiffy = get_loops_per_jiffy();
	use_tsc_delay();
}

void __init tsc_early_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_TSC))
		return;

	if (!determine_cpu_tsc_frequencies())
		return;
	tsc_enable_sched_clock();
}

void __init tsc_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_TSC))
		return;

	/* PIT calibration can fail under host load, leaving tsc_khz == 0. */
	if (!tsc_khz)
		return;

	/*
	 * Nothing keeps wall-clock time, so the TSC is no longer registered
	 * as a clocksource; calibrate_delay() keeps this TSC-based value.
	 */
	lpj_fine = get_loops_per_jiffy();
}
