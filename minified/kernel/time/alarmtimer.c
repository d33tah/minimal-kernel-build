// SPDX-License-Identifier: GPL-2.0
/*
 * Alarmtimer interface
 *
 * This interface provides a timer which is similar to hrtimers,
 * but triggers a RTC alarm if the box is suspend.
 *
 * This interface is influenced by the Android RTC Alarm timer
 * interface.
 *
 * Copyright (C) 2010 IBM Corporation
 *
 * Author: John Stultz <john.stultz@linaro.org>
 */
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/timerqueue.h>
#include <linux/rtc.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/alarmtimer.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/posix-timers.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/compat.h>
#include <linux/module.h>
#include <linux/time_namespace.h>

#include "posix-timers.h"


/**
 * struct alarm_base - Alarm timer bases
 * @lock:		Lock for syncrhonized access to the base
 * @timerqueue:		Timerqueue head managing the list of events
 * @get_ktime:		Function to read the time correlating to the base
 * @get_timespec:	Function to read the namespace time correlating to the base
 * @base_clockid:	clockid for the base
 */
static struct alarm_base {
	spinlock_t		lock;
	struct timerqueue_head	timerqueue;
	ktime_t			(*get_ktime)(void);
	void			(*get_timespec)(struct timespec64 *tp);
	clockid_t		base_clockid;
} alarm_bases[ALARM_NUMTYPE];


static inline int alarmtimer_rtc_interface_setup(void) { return 0; }
static inline void alarmtimer_rtc_interface_remove(void) { }
static inline void alarmtimer_rtc_timer_init(void) { }

/**
 * alarmtimer_enqueue - Adds an alarm timer to an alarm_base timerqueue
 * @base: pointer to the base where the timer is being run
 * @alarm: pointer to alarm being enqueued.
 *
 * Adds alarm to a alarm_base timerqueue
 *
 * Must hold base->lock when calling.
 */
static void alarmtimer_enqueue(struct alarm_base *base, struct alarm *alarm)
{
	if (alarm->state & ALARMTIMER_STATE_ENQUEUED)
		timerqueue_del(&base->timerqueue, &alarm->node);

	timerqueue_add(&base->timerqueue, &alarm->node);
	alarm->state |= ALARMTIMER_STATE_ENQUEUED;
}

/**
 * alarmtimer_dequeue - Removes an alarm timer from an alarm_base timerqueue
 * @base: pointer to the base where the timer is running
 * @alarm: pointer to alarm being removed
 *
 * Removes alarm to a alarm_base timerqueue
 *
 * Must hold base->lock when calling.
 */
static void alarmtimer_dequeue(struct alarm_base *base, struct alarm *alarm)
{
	if (!(alarm->state & ALARMTIMER_STATE_ENQUEUED))
		return;

	timerqueue_del(&base->timerqueue, &alarm->node);
	alarm->state &= ~ALARMTIMER_STATE_ENQUEUED;
}


/**
 * alarmtimer_fired - Handles alarm hrtimer being fired.
 * @timer: pointer to hrtimer being run
 *
 * When a alarm timer fires, this runs through the timerqueue to
 * see which alarms expired, and runs those. If there are more alarm
 * timers queued for the future, we set the hrtimer to fire when
 * the next future alarm timer expires.
 */
static enum hrtimer_restart alarmtimer_fired(struct hrtimer *timer)
{
	struct alarm *alarm = container_of(timer, struct alarm, timer);
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;
	int ret = HRTIMER_NORESTART;
	int restart = ALARMTIMER_NORESTART;

	spin_lock_irqsave(&base->lock, flags);
	alarmtimer_dequeue(base, alarm);
	spin_unlock_irqrestore(&base->lock, flags);

	if (alarm->function)
		restart = alarm->function(alarm, base->get_ktime());

	spin_lock_irqsave(&base->lock, flags);
	if (restart != ALARMTIMER_NORESTART) {
		hrtimer_set_expires(&alarm->timer, alarm->node.expires);
		alarmtimer_enqueue(base, alarm);
		ret = HRTIMER_RESTART;
	}
	spin_unlock_irqrestore(&base->lock, flags);


	return ret;

}

ktime_t alarm_expires_remaining(const struct alarm *alarm)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	return ktime_sub(alarm->node.expires, base->get_ktime());
}
EXPORT_SYMBOL_GPL(alarm_expires_remaining);

static int alarmtimer_suspend(struct device *dev)
{
	return 0;
}

static int alarmtimer_resume(struct device *dev)
{
	return 0;
}

static void
__alarm_init(struct alarm *alarm, enum alarmtimer_type type,
	     enum alarmtimer_restart (*function)(struct alarm *, ktime_t))
{
	timerqueue_init(&alarm->node);
	alarm->timer.function = alarmtimer_fired;
	alarm->function = function;
	alarm->type = type;
	alarm->state = ALARMTIMER_STATE_INACTIVE;
}

/**
 * alarm_init - Initialize an alarm structure
 * @alarm: ptr to alarm to be initialized
 * @type: the type of the alarm
 * @function: callback that is run when the alarm fires
 */
void alarm_init(struct alarm *alarm, enum alarmtimer_type type,
		enum alarmtimer_restart (*function)(struct alarm *, ktime_t))
{
	hrtimer_init(&alarm->timer, alarm_bases[type].base_clockid,
		     HRTIMER_MODE_ABS);
	__alarm_init(alarm, type, function);
}
EXPORT_SYMBOL_GPL(alarm_init);

/**
 * alarm_start - Sets an absolute alarm to fire
 * @alarm: ptr to alarm to set
 * @start: time to run the alarm
 */
void alarm_start(struct alarm *alarm, ktime_t start)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;

	spin_lock_irqsave(&base->lock, flags);
	alarm->node.expires = start;
	alarmtimer_enqueue(base, alarm);
	hrtimer_start(&alarm->timer, alarm->node.expires, HRTIMER_MODE_ABS);
	spin_unlock_irqrestore(&base->lock, flags);

	/* trace_alarmtimer_start(alarm, base->get_ktime()); */
}
EXPORT_SYMBOL_GPL(alarm_start);

/**
 * alarm_start_relative - Sets a relative alarm to fire
 * @alarm: ptr to alarm to set
 * @start: time relative to now to run the alarm
 */
void alarm_start_relative(struct alarm *alarm, ktime_t start)
{
	struct alarm_base *base = &alarm_bases[alarm->type];

	start = ktime_add_safe(start, base->get_ktime());
	alarm_start(alarm, start);
}
EXPORT_SYMBOL_GPL(alarm_start_relative);

void alarm_restart(struct alarm *alarm)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;

	spin_lock_irqsave(&base->lock, flags);
	hrtimer_set_expires(&alarm->timer, alarm->node.expires);
	hrtimer_restart(&alarm->timer);
	alarmtimer_enqueue(base, alarm);
	spin_unlock_irqrestore(&base->lock, flags);
}
EXPORT_SYMBOL_GPL(alarm_restart);

/**
 * alarm_try_to_cancel - Tries to cancel an alarm timer
 * @alarm: ptr to alarm to be canceled
 *
 * Returns 1 if the timer was canceled, 0 if it was not running,
 * and -1 if the callback was running
 */
int alarm_try_to_cancel(struct alarm *alarm)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&base->lock, flags);
	ret = hrtimer_try_to_cancel(&alarm->timer);
	if (ret >= 0)
		alarmtimer_dequeue(base, alarm);
	spin_unlock_irqrestore(&base->lock, flags);

	/* trace_alarmtimer_cancel(alarm, base->get_ktime()); */
	return ret;
}
EXPORT_SYMBOL_GPL(alarm_try_to_cancel);


/**
 * alarm_cancel - Spins trying to cancel an alarm timer until it is done
 * @alarm: ptr to alarm to be canceled
 *
 * Returns 1 if the timer was canceled, 0 if it was not active.
 */
int alarm_cancel(struct alarm *alarm)
{
	for (;;) {
		int ret = alarm_try_to_cancel(alarm);
		if (ret >= 0)
			return ret;
		hrtimer_cancel_wait_running(&alarm->timer);
	}
}
EXPORT_SYMBOL_GPL(alarm_cancel);


u64 alarm_forward(struct alarm *alarm, ktime_t now, ktime_t interval)
{
	u64 overrun = 1;
	ktime_t delta;

	delta = ktime_sub(now, alarm->node.expires);

	if (delta < 0)
		return 0;

	if (unlikely(delta >= interval)) {
		s64 incr = ktime_to_ns(interval);

		overrun = ktime_divns(delta, incr);

		alarm->node.expires = ktime_add_ns(alarm->node.expires,
							incr*overrun);

		if (alarm->node.expires > now)
			return overrun;
		/*
		 * This (and the ktime_add() below) is the
		 * correction for exact:
		 */
		overrun++;
	}

	alarm->node.expires = ktime_add_safe(alarm->node.expires, interval);
	return overrun;
}
EXPORT_SYMBOL_GPL(alarm_forward);

u64 alarm_forward_now(struct alarm *alarm, ktime_t interval)
{
	struct alarm_base *base = &alarm_bases[alarm->type];

	return alarm_forward(alarm, base->get_ktime(), interval);
}
EXPORT_SYMBOL_GPL(alarm_forward_now);



/* Suspend hook structures */
static const struct dev_pm_ops alarmtimer_pm_ops = {
	.suspend = alarmtimer_suspend,
	.resume = alarmtimer_resume,
};

static struct platform_driver alarmtimer_driver = {
	.driver = {
		.name = "alarmtimer",
		.pm = &alarmtimer_pm_ops,
	}
};

static void get_boottime_timespec(struct timespec64 *tp)
{
	ktime_get_boottime_ts64(tp);
	timens_add_boottime(tp);
}

/**
 * alarmtimer_init - Initialize alarm timer code
 *
 * This function initializes the alarm bases and registers
 * the posix clock ids.
 */
static int __init alarmtimer_init(void)
{
	int error;
	int i;

	alarmtimer_rtc_timer_init();

	/* Initialize alarm bases */
	alarm_bases[ALARM_REALTIME].base_clockid = CLOCK_REALTIME;
	alarm_bases[ALARM_REALTIME].get_ktime = &ktime_get_real;
	alarm_bases[ALARM_REALTIME].get_timespec = ktime_get_real_ts64;
	alarm_bases[ALARM_BOOTTIME].base_clockid = CLOCK_BOOTTIME;
	alarm_bases[ALARM_BOOTTIME].get_ktime = &ktime_get_boottime;
	alarm_bases[ALARM_BOOTTIME].get_timespec = get_boottime_timespec;
	for (i = 0; i < ALARM_NUMTYPE; i++) {
		timerqueue_init_head(&alarm_bases[i].timerqueue);
		spin_lock_init(&alarm_bases[i].lock);
	}

	error = alarmtimer_rtc_interface_setup();
	if (error)
		return error;

	error = platform_driver_register(&alarmtimer_driver);
	if (error)
		goto out_if;

	return 0;
out_if:
	alarmtimer_rtc_interface_remove();
	return error;
}
device_initcall(alarmtimer_init);
