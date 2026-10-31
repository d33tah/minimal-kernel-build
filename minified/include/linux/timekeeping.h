#ifndef _LINUX_TIMEKEEPING_H
#define _LINUX_TIMEKEEPING_H

/* Inlined from clocksource_ids.h */
enum clocksource_ids {
	CSID_GENERIC = 0,
	CSID_MAX,
};

void timekeeping_init(void);

extern void ktime_get_coarse_real_ts64(struct timespec64 *ts);

extern void read_persistent_clock64(struct timespec64 *ts);

#endif
