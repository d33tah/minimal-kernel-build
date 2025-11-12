// SPDX-License-Identifier: GPL-2.0
/*
 * Alarmtimer interface - Stubbed minimal implementation
 */
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/timerqueue.h>
#include <linux/alarmtimer.h>

ktime_t alarm_expires_remaining(const struct alarm *alarm) { return 0; }
EXPORT_SYMBOL_GPL(alarm_expires_remaining);

void alarm_init(struct alarm *alarm, enum alarmtimer_type type,
		enum alarmtimer_restart (*function)(struct alarm *, ktime_t)) { }
EXPORT_SYMBOL_GPL(alarm_init);

void alarm_start(struct alarm *alarm, ktime_t start) { }
EXPORT_SYMBOL_GPL(alarm_start);

void alarm_start_relative(struct alarm *alarm, ktime_t start) { }
EXPORT_SYMBOL_GPL(alarm_start_relative);

void alarm_restart(struct alarm *alarm) { }
EXPORT_SYMBOL_GPL(alarm_restart);

int alarm_try_to_cancel(struct alarm *alarm) { return 0; }
EXPORT_SYMBOL_GPL(alarm_try_to_cancel);

int alarm_cancel(struct alarm *alarm) { return 0; }
EXPORT_SYMBOL_GPL(alarm_cancel);

u64 alarm_forward(struct alarm *alarm, ktime_t now, ktime_t interval) { return 1; }
EXPORT_SYMBOL_GPL(alarm_forward);

u64 alarm_forward_now(struct alarm *alarm, ktime_t interval) { return 1; }
EXPORT_SYMBOL_GPL(alarm_forward_now);
