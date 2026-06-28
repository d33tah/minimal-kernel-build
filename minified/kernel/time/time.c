
#include <linux/kernel.h>
#include <linux/timex.h>
#include <linux/timekeeper_internal.h>
#include <linux/errno.h>
#include <linux/syscalls.h>
#include <linux/math64.h>

#include <linux/compat.h>

#include <generated/timeconst.h>
#include "timekeeping.h"

/* gettimeofday body stubbed - init does write(2)+exit only, never reads time */
SYSCALL_DEFINE2(gettimeofday, struct __kernel_old_timeval __user *, tv,
		struct timezone __user *, tz)
{
	return -ENOSYS;
}

/* settimeofday syscall removed - init does write(2)+exit only */

/*
 * Removed: jiffies_to_msecs, __msecs_to_jiffies - never called. The sole
 * msecs_to_jiffies() callsite (softirq.c) passes a compile-time constant, so
 * it constant-folds to the inline _msecs_to_jiffies(); the out-of-line slow
 * path and the reverse conversion are unreferenced under CONFIG_HZ=250.
 */

void set_normalized_timespec64(struct timespec64 *ts, time64_t sec, s64 nsec)
{
	while (nsec >= NSEC_PER_SEC) {
		 
		asm("" : "+rm"(nsec));
		nsec -= NSEC_PER_SEC;
		++sec;
	}
	while (nsec < 0) {
		asm("" : "+rm"(nsec));
		nsec += NSEC_PER_SEC;
		--sec;
	}
	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

struct timespec64 ns_to_timespec64(const s64 nsec)
{
	struct timespec64 ts = { 0, 0 };
	s32 rem;

	if (likely(nsec > 0)) {
		ts.tv_sec = div_u64_rem(nsec, NSEC_PER_SEC, &rem);
		ts.tv_nsec = rem;
	} else if (nsec < 0) {
		 
		ts.tv_sec = -div_u64_rem(-nsec - 1, NSEC_PER_SEC, &rem) - 1;
		ts.tv_nsec = NSEC_PER_SEC - rem - 1;
	}

	return ts;
}


