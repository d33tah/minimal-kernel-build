#ifndef _LINUX_TIMEX_H
#define _LINUX_TIMEX_H
#include <linux/time.h>
/* struct __kernel_timex_timeval, __kernel_timex removed - never used */
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/param.h>
/* random_get_entropy_fallback declaration removed - function never called */
#include <asm/timex.h>
/* random_get_entropy macros removed - never called */
/* shift_right removed - never used */
#define NTP_SCALE_SHIFT		32
#define NTP_INTERVAL_FREQ  (HZ)
#define NTP_INTERVAL_LENGTH (NSEC_PER_SEC/NTP_INTERVAL_FREQ)
#define PIT_TICK_RATE 1193182ul
#endif
