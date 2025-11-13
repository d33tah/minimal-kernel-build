// SPDX-License-Identifier: GPL-2.0
// Stubbed NTP implementation - network time synchronization not needed for minimal kernel

#include <linux/capability.h>
#include <linux/clocksource.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/math64.h>
#include <linux/timex.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/audit.h>

#include "ntp_internal.h"
#include "timekeeping_internal.h"

// Fixed tick length for stub (TICK_NSEC << NTP_SCALE_SHIFT)
// NTP_SCALE_SHIFT is typically 32, TICK_NSEC depends on HZ
static u64 tick_length_stub = ((u64)TICK_NSEC << 32);

// Stub: Clear NTP state (no-op)
void ntp_clear(void)
{
}

// Stub: Return fixed tick length
u64 ntp_tick_length(void)
{
	return tick_length_stub;
}

// Stub: Return no pending leap second
ktime_t ntp_get_next_leap(void)
{
	return KTIME_MAX;
}

// Stub: Handle second overflow (no adjustments)
int second_overflow(time64_t secs)
{
	return 0;
}

// Stub: Update persistent clock (no-op)
int __weak update_persistent_clock64(struct timespec64 now64)
{
	return -ENODEV;
}

// Stub: Notify CMOS timer (no-op)
void ntp_notify_cmos_timer(void)
{
}

// Stub: Handle PPS signal (no-op)
void __hardpps(const struct timespec64 *phase_ts, const struct timespec64 *raw_ts)
{
}

// Stub: Adjust time (return error - not supported)
int __do_adjtimex(struct __kernel_timex *txc, const struct timespec64 *ts,
		  s32 *time_tai, struct audit_ntp_data *ad)
{
	// Return current time without adjustments
	if (txc) {
		txc->time.tv_sec = ts->tv_sec;
		txc->time.tv_usec = ts->tv_nsec / NSEC_PER_USEC;
		txc->tai = *time_tai;
		txc->offset = 0;
		txc->freq = 0;
		txc->maxerror = 0;
		txc->esterror = 0;
		txc->status = STA_UNSYNC;
		txc->constant = 0;
		txc->precision = 1;
		txc->tolerance = 0;
		txc->tick = USEC_PER_SEC / HZ;
	}
	return TIME_ERROR;
}

// Stub: Initialize NTP (no-op)
void __init ntp_init(void)
{
}
