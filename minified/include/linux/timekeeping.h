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
extern void ktime_get_coarse_real_ts64(struct timespec64 *ts);


extern time64_t ktime_get_seconds(void);
extern time64_t ktime_get_real_seconds(void);


enum tk_offsets {
	TK_OFFS_REAL,
	TK_OFFS_BOOT,
	TK_OFFS_TAI,
	TK_OFFS_MAX,
};

extern ktime_t ktime_get(void);
extern ktime_t ktime_get_with_offset(enum tk_offsets offs);

static inline ktime_t ktime_get_real(void)
{
	return ktime_get_with_offset(TK_OFFS_REAL);
}


static inline ktime_t ktime_get_boottime(void)
{
	return ktime_get_with_offset(TK_OFFS_BOOT);
}

static inline ktime_t ktime_get_clocktai(void)
{
	return ktime_get_with_offset(TK_OFFS_TAI);
}
static inline void ktime_get_boottime_ts64(struct timespec64 *ts)
{
	*ts = ktime_to_timespec64(ktime_get_boottime());
}

extern void read_persistent_clock64(struct timespec64 *ts);
void read_persistent_wall_and_boot_offset(struct timespec64 *wall_clock,
					  struct timespec64 *boot_offset);

#endif
