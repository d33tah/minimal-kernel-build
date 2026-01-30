
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/timex.h>
#include <linux/capability.h>
#include <linux/timekeeper_internal.h>
#include <linux/errno.h>
#include <linux/syscalls.h>
/* linux/security.h removed - unused */
/* linux/fs.h removed - unused */
#include <linux/math64.h>
/* linux/ptrace.h removed - unused */

#include <linux/uaccess.h>
/* linux/compat.h removed - unused */
#include <asm/unistd.h>

#include <generated/timeconst.h>
#include "timekeeping.h"

/* struct timezone sys_tz removed - never used */

#ifdef __ARCH_WANT_SYS_TIME

SYSCALL_DEFINE1(time, __kernel_old_time_t __user *, tloc)
{
	/* Stub: time not needed for Hello World */
	return 0;
}

SYSCALL_DEFINE1(stime, __kernel_old_time_t __user *, tptr)
{
	return -EPERM;
}

#endif

SYSCALL_DEFINE2(gettimeofday, struct __kernel_old_timeval __user *, tv,
		struct timezone __user *, tz)
{
	/* Stub: gettimeofday not needed for Hello World */
	return 0;
}

SYSCALL_DEFINE2(settimeofday, struct __kernel_old_timeval __user *, tv,
		struct timezone __user *, tz)
{
	/* Stub: setting time not needed for minimal kernel */
	return -EPERM;
}

/* jiffies_to_msecs removed - never called */
/* jiffies_to_usecs removed - never called */

time64_t mktime64(const unsigned int year0, const unsigned int mon0,
		  const unsigned int day, const unsigned int hour,
		  const unsigned int min, const unsigned int sec)
{
	unsigned int mon = mon0, year = year0;

	if (0 >= (int)(mon -= 2)) {
		mon += 12;
		year -= 1;
	}

	return ((((time64_t)(year / 4 - year / 100 + year / 400 +
			     367 * mon / 12 + day) +
		  year * 365 - 719499) *
			 24 +
		 hour) * 60 +
		min) * 60 +
	       sec;
}

/* ns_to_timespec64 removed - never called */

unsigned long __msecs_to_jiffies(const unsigned int m)
{
	if ((int)m < 0)
		return MAX_JIFFY_OFFSET;
	return _msecs_to_jiffies(m);
}

/* Removed: __usecs_to_jiffies, timespec64_to_jiffies, nsecs_to_jiffies64,
   nsecs_to_jiffies, get_timespec64, put_timespec64 - dead code */
