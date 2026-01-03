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


extern void ktime_get_ts64(struct timespec64 *ts);
extern void ktime_get_real_ts64(struct timespec64 *tv);
/* ktime_get_coarse_real_ts64 removed - never called */

extern time64_t ktime_get_seconds(void);
extern time64_t ktime_get_real_seconds(void);


enum tk_offsets {
	TK_OFFS_REAL,
	TK_OFFS_BOOT,
	TK_OFFS_TAI,
	TK_OFFS_MAX,
};

extern ktime_t ktime_get(void);
/* ktime_get_with_offset removed - never called */

/* ktime_get_real, ktime_get_boottime, ktime_get_clocktai removed - unused */
extern void read_persistent_clock64(struct timespec64 *ts);
void read_persistent_wall_and_boot_offset(struct timespec64 *wall_clock,
					  struct timespec64 *boot_offset);

#endif
