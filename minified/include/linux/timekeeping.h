#ifndef _LINUX_TIMEKEEPING_H
#define _LINUX_TIMEKEEPING_H

#include <linux/errno.h>

/* Inlined from clocksource_ids.h */
enum clocksource_ids {
	CSID_GENERIC		= 0,
	CSID_MAX,
};

void timekeeping_init(void);
/* timekeeping_suspended now static in timekeeping.c */


/* ktime_get_ts64, ktime_get_real_ts64 removed - never called */
/* ktime_get_coarse_real_ts64 removed - never called */

/* ktime_get_seconds, ktime_get_real_seconds removed - never called */
/* enum tk_offsets removed - never used */

extern ktime_t ktime_get(void);
/* ktime_get_with_offset removed - never called */

/* ktime_get_real, ktime_get_boottime, ktime_get_clocktai removed - unused */
extern void read_persistent_clock64(struct timespec64 *ts);
void read_persistent_wall_and_boot_offset(struct timespec64 *wall_clock,
					  struct timespec64 *boot_offset);

#endif
