#ifndef _LINUX_TIMEKEEPING_H
#define _LINUX_TIMEKEEPING_H

/* linux/errno.h removed - no errno constants used */

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

/* ktime_get, read_persistent_clock64 removed - never called */
/* read_persistent_wall_and_boot_offset removed - never called */

#endif
