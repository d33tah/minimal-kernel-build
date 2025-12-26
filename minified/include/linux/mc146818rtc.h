#ifndef _MC146818RTC_H
#define _MC146818RTC_H

#include <asm/io.h>
#include <linux/rtc.h>
#include <asm/mc146818rtc.h>
#include <linux/bcd.h>
#include <linux/delay.h>

#ifdef __KERNEL__
#include <linux/spinlock.h>
extern spinlock_t rtc_lock;
#endif

#define RTC_SECONDS		0
#define RTC_MINUTES		2
#define RTC_HOURS		4
#define RTC_DAY_OF_MONTH	7
#define RTC_MONTH		8
#define RTC_YEAR		9
#define RTC_REG_A		10
#define RTC_REG_B		11
#define RTC_FREQ_SELECT	RTC_REG_A
#define RTC_UIP		0x80
#define RTC_CONTROL	RTC_REG_B
#define RTC_DM_BINARY 0x04

int mc146818_get_time(struct rtc_time *time);
int mc146818_set_time(struct rtc_time *time);
bool mc146818_avoid_UIP(void (*callback)(unsigned char seconds, void *param), void *param);

#endif  
