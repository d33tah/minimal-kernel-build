#ifndef _LINUX_CLOCKSOURCE_H
#define _LINUX_CLOCKSOURCE_H

#include <linux/ktime.h>
#include <asm/io.h>

struct clocksource {
	u64 (*read)(struct clocksource *cs);
	u64 mask;
	u32 mult, shift, maxadj;
	const char *name;
	struct list_head list;
	int rating;
	enum clocksource_ids id;
	int (*enable)(struct clocksource *cs);
};

#define CLOCKSOURCE_MASK(bits) GENMASK_ULL((bits) - 1, 0)

extern struct clocksource *__init clocksource_default_clock(void);

extern void clocks_calc_mult_shift(u32 *mult, u32 *shift, u32 from, u32 to,
				   u32 minsec);

extern int __clocksource_register_scale(struct clocksource *cs, u32 scale,
					u32 freq);
extern void __clocksource_update_freq_scale(struct clocksource *cs, u32 scale,
					    u32 freq);

static inline int __clocksource_register(struct clocksource *cs)
{
	return __clocksource_register_scale(cs, 1, 0);
}

static inline int clocksource_register_khz(struct clocksource *cs, u32 khz)
{
	return __clocksource_register_scale(cs, 1000, khz);
}

extern int timekeeping_notify(struct clocksource *clock);

#endif
