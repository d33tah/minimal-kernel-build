#ifndef _LINUX_TIME_H
#define _LINUX_TIME_H
#include <linux/cache.h>
#include <linux/math64.h>
#include <linux/time64.h>
extern time64_t mktime64(const unsigned int year, const unsigned int mon, const unsigned int day, const unsigned int hour, const unsigned int min, const unsigned int sec);
typedef s32 old_time32_t;
struct old_timespec32 { old_time32_t tv_sec; s32 tv_nsec; };
struct timens_offset { s64 sec; u64 nsec; };
#endif
