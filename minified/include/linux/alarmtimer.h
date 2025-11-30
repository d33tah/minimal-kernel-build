 
#ifndef _LINUX_ALARMTIMER_H
#define _LINUX_ALARMTIMER_H

#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/timerqueue.h>

struct rtc_device;

enum alarmtimer_type {
	ALARM_REALTIME,
};

enum alarmtimer_restart {
	ALARMTIMER_NORESTART,
	ALARMTIMER_RESTART,
};


#define ALARMTIMER_STATE_INACTIVE	0x00
#define ALARMTIMER_STATE_ENQUEUED	0x01

 
struct alarm;

void alarm_init(struct alarm *alarm, enum alarmtimer_type type,
		enum alarmtimer_restart (*function)(struct alarm *, ktime_t));
void alarm_start(struct alarm *alarm, ktime_t start);
void alarm_start_relative(struct alarm *alarm, ktime_t start);
void alarm_restart(struct alarm *alarm);
int alarm_try_to_cancel(struct alarm *alarm);
int alarm_cancel(struct alarm *alarm);

u64 alarm_forward(struct alarm *alarm, ktime_t now, ktime_t interval);
u64 alarm_forward_now(struct alarm *alarm, ktime_t interval);
ktime_t alarm_expires_remaining(const struct alarm *alarm);

static inline struct rtc_device *alarmtimer_get_rtcdev(void) { return NULL; }

#endif
