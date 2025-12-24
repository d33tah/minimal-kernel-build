#ifndef _LINUX_TIME_H
#define _LINUX_TIME_H
#include <linux/cache.h>
#include <linux/math64.h>
#include <linux/time64.h>
int get_timespec64(struct timespec64 *ts, const struct __kernel_timespec __user *uts);
int put_timespec64(const struct timespec64 *ts, struct __kernel_timespec __user *uts);
int get_itimerspec64(struct itimerspec64 *it, const struct __kernel_itimerspec __user *uit);
int put_itimerspec64(const struct itimerspec64 *it, struct __kernel_itimerspec __user *uit);
extern time64_t mktime64(const unsigned int year, const unsigned int mon, const unsigned int day, const unsigned int hour, const unsigned int min, const unsigned int sec);
struct tm { int tm_sec; int tm_min; int tm_hour; int tm_mday; int tm_mon; long tm_year; int tm_wday; int tm_yday; };
#include <linux/time32.h>
struct timens_offset { s64 sec; u64 nsec; };
#endif
