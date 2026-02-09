/* capability.h, errno.h, uaccess.h removed - unused */
/* linux/export.h removed - no EXPORT_SYMBOL */
#include <linux/kernel.h>
#include <linux/timex.h>
#include <linux/timekeeper_internal.h>
#include <linux/syscalls.h>
/* linux/security.h, linux/fs.h, linux/ptrace.h, linux/compat.h removed - unused */
#include <linux/math64.h>
#include <asm/unistd.h>

#include <generated/timeconst.h>
#include "timekeeping.h"

/* struct timezone sys_tz removed - never used */

/* time/stime syscalls removed - __ARCH_WANT_SYS_TIME not defined */

/* gettimeofday, settimeofday syscalls removed - not in syscall table */

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

/* __msecs_to_jiffies removed - never called (~6 LOC) */

/* Removed: __usecs_to_jiffies, timespec64_to_jiffies, nsecs_to_jiffies64,
   nsecs_to_jiffies, get_timespec64, put_timespec64 - dead code */
