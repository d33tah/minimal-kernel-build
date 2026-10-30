#ifndef _LINUX_TIME64_H
#define _LINUX_TIME64_H

#include <linux/math64.h>

/* Inlined from vdso/time64.h */
#define MSEC_PER_SEC 1000L
#define NSEC_PER_USEC 1000L
#define NSEC_PER_MSEC 1000000L
#define NSEC_PER_SEC 1000000000L

typedef __s64 time64_t;

struct timespec64 {
	time64_t tv_sec;
	long tv_nsec;
};

#define TIME64_MAX ((s64) ~((u64)1 << 63))
#define TIME64_MIN (-TIME64_MAX - 1)

#define KTIME_MAX ((s64) ~((u64)1 << 63))
#define KTIME_MIN (-KTIME_MAX - 1)
#define KTIME_SEC_MAX (KTIME_MAX / NSEC_PER_SEC)
#define KTIME_SEC_MIN (KTIME_MIN / NSEC_PER_SEC)

#define TIME_UPTIME_SEC_MAX (30LL * 365 * 24 * 3600)
#define TIME_SETTOD_SEC_MAX (KTIME_SEC_MAX - TIME_UPTIME_SEC_MAX)

static inline int timespec64_equal(const struct timespec64 *a,
				   const struct timespec64 *b)
{
	return (a->tv_sec == b->tv_sec) && (a->tv_nsec == b->tv_nsec);
}

static inline bool timespec64_valid(const struct timespec64 *ts)
{
	if (ts->tv_sec < 0)
		return false;

	return (unsigned long)ts->tv_nsec < NSEC_PER_SEC;
}

static inline bool timespec64_valid_settod(const struct timespec64 *ts)
{
	if (!timespec64_valid(ts))
		return false;

	return (unsigned long long)ts->tv_sec < TIME_SETTOD_SEC_MAX;
}

static inline s64 timespec64_to_ns(const struct timespec64 *ts)
{
	if (ts->tv_sec >= KTIME_SEC_MAX)
		return KTIME_MAX;

	if (ts->tv_sec <= KTIME_SEC_MIN)
		return KTIME_MIN;

	return ((s64)ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
}

#endif
