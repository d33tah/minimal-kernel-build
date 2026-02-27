#ifndef _LINUX_CLOCKSOURCE_H
#define _LINUX_CLOCKSOURCE_H

#include <linux/timex.h>
#include <linux/timer.h>
void timekeeping_init(void);

struct clocksource;
struct module;


struct clocksource {
	u64			(*read)(struct clocksource *cs);
	u64			mask;
	u32			mult;
	u32			shift;
	u32			maxadj;
	u32			uncertainty_margin;
	u64			max_cycles;
	const char		*name;
	struct list_head	list;
	int			rating;
	unsigned long		flags;
};

#define CLOCK_SOURCE_IS_CONTINUOUS		0x01
#define CLOCK_SOURCE_VALID_FOR_HRES		0x20
#define CLOCKSOURCE_MASK(bits) GENMASK_ULL((bits) - 1, 0)

extern struct clocksource * __init clocksource_default_clock(void);

extern void
clocks_calc_mult_shift(u32 *mult, u32 *shift, u32 from, u32 to, u32 minsec);

extern int
__clocksource_register_scale(struct clocksource *cs, u32 scale, u32 freq);
extern void
__clocksource_update_freq_scale(struct clocksource *cs, u32 scale, u32 freq);

static inline int __clocksource_register(struct clocksource *cs)
{
	return __clocksource_register_scale(cs, 1, 0);
}

#endif
