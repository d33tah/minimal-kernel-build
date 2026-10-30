#ifndef _LINUX_TIMEKEEPING_H
#define _LINUX_TIMEKEEPING_H

#include <linux/errno.h>

/* Inlined from clocksource_ids.h */
enum clocksource_ids {
	CSID_GENERIC = 0,
	CSID_MAX,
};

void timekeeping_init(void);

extern void ktime_get_coarse_real_ts64(struct timespec64 *ts);

extern time64_t ktime_get_real_seconds(void);

extern void read_persistent_clock64(struct timespec64 *ts);
void read_persistent_wall_and_boot_offset(struct timespec64 *wall_clock,
					  struct timespec64 *boot_offset);

#endif
