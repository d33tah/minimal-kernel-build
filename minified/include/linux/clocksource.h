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

#if defined(CONFIG_ARCH_CLOCKSOURCE_DATA) || \
    defined(CONFIG_GENERIC_GETTIMEOFDAY)
#include <asm/clocksource.h>
#endif

#include <vdso/clocksource.h>

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

#define CLOCK_SOURCE_WATCHDOG			0x10
#define CLOCK_SOURCE_VALID_FOR_HRES		0x20
#define CLOCK_SOURCE_UNSTABLE			0x40
#define CLOCK_SOURCE_SUSPEND_NONSTOP		0x80
#define CLOCK_SOURCE_RESELECT			0x100
#define CLOCK_SOURCE_VERIFY_PERCPU		0x200
#define CLOCKSOURCE_MASK(bits) GENMASK_ULL((bits) - 1, 0)

/* clocksource_freq2mult, clocksource_khz2mult, clocksource_hz2mult removed - unused */

static inline s64 clocksource_cyc2ns(u64 cycles, u32 mult, u32 shift)
{
	return ((u64) cycles * mult) >> shift;
}


extern int clocksource_unregister(struct clocksource*);
extern void clocksource_touch_watchdog(void);
extern void clocksource_change_rating(struct clocksource *cs, int rating);
extern void clocksource_suspend(void);
extern void clocksource_resume(void);
extern struct clocksource * __init clocksource_default_clock(void);
extern void clocksource_mark_unstable(struct clocksource *cs);
extern void
clocksource_start_suspend_timing(struct clocksource *cs, u64 start_cycles);
extern u64 clocksource_stop_suspend_timing(struct clocksource *cs, u64 now);

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

static inline int clocksource_register_hz(struct clocksource *cs, u32 hz)
{
	return __clocksource_register_scale(cs, 1, hz);
}

static inline int clocksource_register_khz(struct clocksource *cs, u32 khz)
{
	return __clocksource_register_scale(cs, 1000, khz);
}

/* __clocksource_update_freq_hz, __clocksource_update_freq_khz removed - unused */

extern void clocksource_arch_init(struct clocksource *cs);

extern int timekeeping_notify(struct clocksource *clock);

extern u64 clocksource_mmio_readl_up(struct clocksource *);
extern u64 clocksource_mmio_readl_down(struct clocksource *);
extern u64 clocksource_mmio_readw_up(struct clocksource *);
extern u64 clocksource_mmio_readw_down(struct clocksource *);

extern int clocksource_mmio_init(void __iomem *, const char *,
	unsigned long, int, unsigned, u64 (*)(struct clocksource *));

extern int clocksource_i8253_init(void);

#define TIMER_OF_DECLARE(name, compat, fn) \
	OF_DECLARE_1_RET(timer, name, compat, fn)

/* timer_probe removed - unused */

#define TIMER_ACPI_DECLARE(name, table_id, fn)		\
	ACPI_DECLARE_PROBE_ENTRY(timer, name, table_id, 0, NULL, 0, fn)

extern ulong max_cswd_read_retries;
void clocksource_verify_percpu(struct clocksource *cs);

#endif  
