#ifndef _LINUX_CLOCKSOURCE_H
#define _LINUX_CLOCKSOURCE_H

#include <linux/types.h>
#include <linux/timex.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <asm/div64.h>
#include <asm/io.h>


/*
 * Dropped the always-false `defined(CONFIG_ARCH_CLOCKSOURCE_DATA)` OR-operand:
 * CONFIG_ARCH_CLOCKSOURCE_DATA is never defined in this x86-32 build (no Kconfig
 * selects it; this was its sole tree-wide occurrence). The <asm/clocksource.h>
 * include is gated solely by CONFIG_GENERIC_GETTIMEOFDAY (=y here), which alone
 * kept the original OR-arm live -- so this is byte-identical preprocessor output.
 */
#ifdef CONFIG_GENERIC_GETTIMEOFDAY
#include <asm/clocksource.h>
#endif

#include <linux/limits.h>
#include <asm/vdso/clocksource.h>

/* Inlined from vdso/clocksource.h */
enum vdso_clock_mode {
	VDSO_CLOCKMODE_NONE,
	VDSO_ARCH_CLOCKMODES,
	VDSO_CLOCKMODE_MAX,
};

struct clocksource {
	u64			(*read)(struct clocksource *cs);
	u64			mask;
	u32			mult;
	u32			shift;
	u32			maxadj;
	u32			uncertainty_margin;
	const char		*name;
	struct list_head	list;
	int			rating;
	enum clocksource_ids	id;
	enum vdso_clock_mode	vdso_clock_mode;
	unsigned long		flags;

	int			(*enable)(struct clocksource *cs);
};

#define CLOCK_SOURCE_IS_CONTINUOUS		0x01
#define CLOCK_SOURCE_MUST_VERIFY		0x02

#define CLOCK_SOURCE_VALID_FOR_HRES		0x20
#define CLOCK_SOURCE_SUSPEND_NONSTOP		0x80
#define CLOCK_SOURCE_VERIFY_PERCPU		0x200
#define CLOCKSOURCE_MASK(bits) GENMASK_ULL((bits) - 1, 0)


extern int clocksource_unregister(struct clocksource*);
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

static inline int clocksource_register_khz(struct clocksource *cs, u32 khz)
{
	return __clocksource_register_scale(cs, 1000, khz);
}


extern void clocksource_arch_init(struct clocksource *cs);

extern int timekeeping_notify(struct clocksource *clock);


#endif
