// SPDX-License-Identifier: GPL-2.0-only
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

/*
 * This reads the current MSB of the PIT counter, and
 * checks if we are running on sufficiently fast and
 * non-virtualized hardware.
 *
 * Our expectations are:
 *
 *  - the PIT is running at roughly 1.19MHz
 *
 *  - each IO is going to take about 1us on real hardware,
 *    but we allow it to be much faster (by a factor of 10) or
 *    _slightly_ slower (ie we allow up to a 2us read+counter
 *    update - anything else implies a unacceptably slow CPU
 *    or PIT for the fast calibration to work.
 *
 *  - with 256 PIT ticks to read the value, we have 214us to
 *    see the same MSB (and overhead like doing a single TSC
 *    read per MSB value etc).
 *
 *  - We're doing 2 reads per loop (LSB, MSB), and we expect
 *    them each to take about a microsecond on real hardware.
 *    So we expect a count value of around 100. But we'll be
 *    generous, and accept anything over 50.
 *
 *  - if the PIT is stuck, and we see *many* more reads, we
 *    return early (and the next caller of pit_expect_msb()
 *    then consider it a failure when they don't see the
 *    next expected value).
 *
 * These expectations mean that we know that we have seen the
 * transition from one expected value to another with a fairly
 * high accuracy, and we didn't miss any events. We can thus
 * use the TSC value at the transitions to calculate a pretty
 * good value for the TSC frequency.
 */
static inline int pit_verify_msb(unsigned char val)
{
	/* Ignore LSB */
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

	/*
	 * We require _some_ success, but the quality control
	 * will be based on the error terms on the TSC values.
	 */
	return count > 5;
}

/*
 * How many MSB values do we want to see? We aim for
 * a maximum error rate of 500ppm (in practice the
 * real error is much smaller), but refuse to spend
 * more than 50ms on it.
 */
#define MAX_QUICK_PIT_MS 50
#define MAX_QUICK_PIT_ITERATIONS (MAX_QUICK_PIT_MS * PIT_TICK_RATE / 1000 / 256)

static unsigned long quick_pit_calibrate(void)
{
	int i;
	u64 tsc, delta;
	unsigned long d1, d2;

	if (!has_legacy_pic())
		return 0;

	/* Set the Gate high, disable speaker */
	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	/*
	 * Counter 2, mode 0 (one-shot), binary count
	 *
	 * NOTE! Mode 2 decrements by two (and then the
	 * output is flipped each time, giving the same
	 * final output frequency as a decrement-by-one),
	 * so mode 0 is much better when looking at the
	 * individual counts.
	 */
	outb(0xb0, 0x43);

	/* Start at 0xffff */
	outb(0xff, 0x42);
	outb(0xff, 0x42);

	/*
	 * The PIT starts counting at the next edge, so we
	 * need to delay for a microsecond. The easiest way
	 * to do that is to just read back the 16-bit counter
	 * once from the PIT.
	 */
	pit_verify_msb(0);

	if (pit_expect_msb(0xff, &tsc, &d1)) {
		for (i = 1; i <= MAX_QUICK_PIT_ITERATIONS; i++) {
			if (!pit_expect_msb(0xff - i, &delta, &d2))
				break;

			delta -= tsc;

			/*
			 * Extrapolate the error and fail fast if the error will
			 * never be below 500 ppm.
			 */
			if (i == 1 &&
			    d1 + d2 >= (delta * MAX_QUICK_PIT_ITERATIONS) >> 11)
				return 0;

			/*
			 * Iterate until the error is less than 500 ppm
			 */
			if (d1 + d2 >= delta >> 11)
				continue;

			/*
			 * Check the PIT one more time to verify that
			 * all TSC reads were stable wrt the PIT.
			 *
			 * This also guarantees serialization of the
			 * last cycle read ('d2') in pit_expect_msb.
			 */
			if (!pit_verify_msb(0xfe - i))
				break;
			goto success;
		}
	}
	return 0;

success:

	/*
	 * Ok, if we get here, then we've seen the
	 * MSB of the PIT decrement 'i' times, and the
	 * error has shrunk to less than 500 ppm.
	 *
	 * As a result, we can depend on there not being
	 * any odd delays anywhere, and the TSC reads are
	 * reliable (within the error).
	 *
	 * kHz = ticks / time-in-seconds / 1000;
	 * kHz = (t2 - t1) / (I * 256 / PIT_TICK_RATE) / 1000
	 * kHz = ((t2 - t1) * PIT_TICK_RATE) / (I * 256 * 1000)
	 */
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
