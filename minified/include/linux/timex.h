#ifndef _LINUX_TIMEX_H
#define _LINUX_TIMEX_H
#include <linux/time.h>
/* struct __kernel_timex_timeval, __kernel_timex removed - never used */
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/param.h>
unsigned long random_get_entropy_fallback(void);
#include <asm/timex.h>
#ifndef random_get_entropy
#ifdef get_cycles
#define random_get_entropy()	((unsigned long)get_cycles())
#else
#define random_get_entropy()	random_get_entropy_fallback()
#endif
#endif
#define shift_right(x, s) ({ __typeof__(x) __x = (x); __typeof__(s) __s = (s); __x < 0 ? -(-__x >> __s) : __x >> __s; })
#define NTP_SCALE_SHIFT		32
#define NTP_INTERVAL_FREQ  (HZ)
#define NTP_INTERVAL_LENGTH (NSEC_PER_SEC/NTP_INTERVAL_FREQ)
#define PIT_TICK_RATE 1193182ul
#endif
