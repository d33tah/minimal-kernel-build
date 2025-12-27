#ifndef _LINUX_CLOCKSOURCE_H
#define _LINUX_CLOCKSOURCE_H

#include <linux/types.h>
#include <linux/timex.h>
#include <linux/time.h>
#include <linux/list.h>
#include <linux/cache.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/timekeeping.h>
#include <asm/div64.h>
#include <asm/io.h>

struct clocksource;
struct module;

/* CONFIG_ARCH_CLOCKSOURCE_DATA/GENERIC_GETTIMEOFDAY not defined */

#include <linux/limits.h>
#include <asm/vdso/clocksource.h>

/* Inlined from vdso/clocksource.h */
enum vdso_clock_mode {
	VDSO_CLOCKMODE_NONE,
	VDSO_ARCH_CLOCKMODES,
	VDSO_CLOCKMODE_MAX,
	VDSO_CLOCKMODE_TIMENS = INT_MAX
};

struct clocksource {
	u64			(*read)(struct clocksource *cs);
	u64			mask;
	u32			mult;
	u32			shift;
	u64			max_idle_ns;
	u32			maxadj;
	u32			uncertainty_margin;
	u64			max_cycles;
	const char		*name;
	struct list_head	list;
	int			rating;
	enum clocksource_ids	id;
	enum vdso_clock_mode	vdso_clock_mode;
	unsigned long		flags;

	int			(*enable)(struct clocksource *cs);
	void			(*disable)(struct clocksource *cs);
	void			(*suspend)(struct clocksource *cs);
	void			(*resume)(struct clocksource *cs);
	void			(*mark_unstable)(struct clocksource *cs);
	void			(*tick_stable)(struct clocksource *cs);

	 
	 
	struct list_head	wd_list;
	u64			cs_last;
	u64			wd_last;
	struct module		*owner;
};

#define CLOCK_SOURCE_IS_CONTINUOUS		0x01
#define CLOCK_SOURCE_MUST_VERIFY		0x02
#define CLOCK_SOURCE_VALID_FOR_HRES		0x20
#define CLOCK_SOURCE_UNSTABLE			0x40
#define CLOCK_SOURCE_SUSPEND_NONSTOP		0x80
#define CLOCK_SOURCE_VERIFY_PERCPU		0x200
#define CLOCKSOURCE_MASK(bits) GENMASK_ULL((bits) - 1, 0)


static inline s64 clocksource_cyc2ns(u64 cycles, u32 mult, u32 shift)
{
	return ((u64) cycles * mult) >> shift;
}


extern int clocksource_unregister(struct clocksource*);
extern struct clocksource * __init clocksource_default_clock(void);
extern void clocksource_mark_unstable(struct clocksource *cs);

extern u64
clocks_calc_max_nsecs(u32 mult, u32 shift, u32 maxadj, u64 mask, u64 *max_cycles);
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

static inline int clocksource_register_khz(struct clocksource *cs, u32 khz)
{
	return __clocksource_register_scale(cs, 1000, khz);
}


extern void clocksource_arch_init(struct clocksource *cs);

extern int timekeeping_notify(struct clocksource *clock);

#endif
