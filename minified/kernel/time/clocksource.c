// SPDX-License-Identifier: GPL-2.0+
/*
 * Stubbed clocksource management for minimal kernel
 * Original file contained complex clocksource driver management (~1,277 LOC)
 * Reduced to minimal stubs for basic operation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <linux/kthread.h>
#include <linux/prandom.h>
#include <linux/cpu.h>

#include "tick-internal.h"
#include "timekeeping_internal.h"

/**
 * clocks_calc_mult_shift - simplified stub for mult/shift calculation
 */
void clocks_calc_mult_shift(u32 *mult, u32 *shift, u32 from, u32 to, u32 maxsec)
{
	/* Simplified calculation - just provide reasonable defaults */
	*shift = 24;
	*mult = (to << 24) / from;
}

/**
 * clocks_calc_max_nsecs - stub for max nanoseconds calculation
 */
u64 clocks_calc_max_nsecs(u32 mult, u32 shift, u32 maxadj, u64 mask, u64 *max_cyc)
{
	u64 max_nsecs = (mask >> shift) * mult;
	if (max_cyc)
		*max_cyc = mask;
	return max_nsecs;
}

/**
 * __clocksource_update_freq_scale - stub for frequency scale update
 */
void __clocksource_update_freq_scale(struct clocksource *cs, u32 scale, u32 freq)
{
	/* Stub - no-op for minimal kernel */
}

/**
 * __clocksource_register_scale - stub for clocksource registration
 */
int __clocksource_register_scale(struct clocksource *cs, u32 scale, u32 freq)
{
	/* Stub - always succeed */
	return 0;
}

/**
 * clocksource_mark_unstable - stub for marking clocksource as unstable
 */
void clocksource_mark_unstable(struct clocksource *cs)
{
	/* Stub - no-op */
}

/**
 * clocksource_verify_percpu - stub for per-CPU verification
 */
void clocksource_verify_percpu(struct clocksource *cs)
{
	/* Stub - no-op */
}

/**
 * clocksource_start_suspend_timing - stub for suspend timing start
 */
void clocksource_start_suspend_timing(struct clocksource *cs, u64 start_cycles)
{
	/* Stub - no-op */
}

/**
 * clocksource_stop_suspend_timing - stub for suspend timing stop
 */
u64 clocksource_stop_suspend_timing(struct clocksource *cs, u64 cycle_now)
{
	/* Stub - return 0 elapsed time */
	return 0;
}

/**
 * clocksource_suspend - stub for clocksource suspend
 */
void clocksource_suspend(void)
{
	/* Stub - no-op */
}

/**
 * clocksource_resume - stub for clocksource resume
 */
void clocksource_resume(void)
{
	/* Stub - no-op */
}

/**
 * clocksource_touch_watchdog - stub for watchdog touch
 */
void clocksource_touch_watchdog(void)
{
	/* Stub - no-op */
}

/**
 * clocksource_change_rating - stub for changing clocksource rating
 */
void clocksource_change_rating(struct clocksource *cs, int rating)
{
	/* Stub - no-op */
}

/**
 * clocksource_unregister - stub for clocksource unregistration
 */
int clocksource_unregister(struct clocksource *cs)
{
	/* Stub - always succeed */
	return 0;
}
