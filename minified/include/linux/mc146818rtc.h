#ifndef _MC146818RTC_H
#define _MC146818RTC_H

#include <asm/io.h>
/* rtc.h removed - struct rtc_time never used */
#include <asm/mc146818rtc.h>
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

/* mc146818_get_time/set_time/avoid_UIP removed - never called */

#endif  
