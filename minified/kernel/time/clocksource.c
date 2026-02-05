/* Stub: Clocksource - minimal for single-CPU hello-world kernel */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
/* linux/kthread.h, linux/cpu.h removed - unused */

#include "tick-internal.h"
#include "timekeeping_internal.h"

void clocks_calc_mult_shift(u32 *mult, u32 *shift, u32 from, u32 to, u32 maxsec)
{
	u64 tmp;
	u32 sft, sftacc = 32;

	tmp = ((u64)maxsec * from) >> 32;
	while (tmp) {
		tmp >>= 1;
		sftacc--;
	}

	for (sft = 32; sft > 0; sft--) {
		tmp = (u64)to << sft;
		tmp += from / 2;
		do_div(tmp, from);
		if ((tmp >> sftacc) == 0)
			break;
	}
	*mult = tmp;
	*shift = sft;
}

static struct clocksource *curr_clocksource;
static LIST_HEAD(clocksource_list);
static DEFINE_MUTEX(clocksource_mutex);
/* override_name removed - always empty, never set */
static int finished_booting;
static DEFINE_SPINLOCK(watchdog_lock);

/* Stub: watchdog functions - not needed for minimal single-CPU kernel */
static inline void clocksource_watchdog_lock(unsigned long *flags)
{
	spin_lock_irqsave(&watchdog_lock, *flags);
}

static inline void clocksource_watchdog_unlock(unsigned long *flags)
{
	spin_unlock_irqrestore(&watchdog_lock, *flags);
}

/* clocksource_mark_unstable removed - empty stub */

static u32 clocksource_max_adjustment(struct clocksource *cs)
{
	u64 ret;
	ret = (u64)cs->mult * 11;
	do_div(ret, 100);
	return (u32)ret;
}

u64 clocks_calc_max_nsecs(u32 mult, u32 shift, u32 maxadj, u64 mask,
			  u64 *max_cyc)
{
	u64 max_nsecs, max_cycles;

	max_cycles = ULLONG_MAX;
	do_div(max_cycles, mult + maxadj);

	max_cycles = min(max_cycles, mask);
	max_nsecs = ((u64)max_cycles * (mult - maxadj)) >> shift;

	if (max_cyc)
		*max_cyc = max_cycles;

	max_nsecs >>= 1;

	return max_nsecs;
}

/* clocksource_find_best removed - inlined into single caller (~13 LOC) */

static void __clocksource_select(bool skipcur)
{
	struct clocksource *best = NULL, *cs;

	/* Inlined clocksource_find_best */
	if (finished_booting && !list_empty(&clocksource_list)) {
		list_for_each_entry(cs, &clocksource_list, list) {
			if (skipcur && cs == curr_clocksource)
				continue;
			best = cs;
			break;
		}
	}
	if (!best)
		return;

	/* override_name was always empty - override logic removed */
	/* timekeeping_notify call inlined - always returned 0 (success) */
	if (curr_clocksource != best) {
		curr_clocksource = best;
	}
}

static int __init clocksource_done_booting(void)
{
	mutex_lock(&clocksource_mutex);
	curr_clocksource = clocksource_default_clock();
	finished_booting = 1;
	__clocksource_select(false);
	mutex_unlock(&clocksource_mutex);
	return 0;
}
fs_initcall(clocksource_done_booting);

/* clocksource_enqueue inlined into __clocksource_register_scale */
/* clocksource_enqueue_watchdog removed - inlined into single caller (~5 LOC) */

void __clocksource_update_freq_scale(struct clocksource *cs, u32 scale,
				     u32 freq)
{
	u64 sec;

	if (freq) {
		sec = cs->mask;
		do_div(sec, freq);
		do_div(sec, scale);
		if (!sec)
			sec = 1;
		else if (sec > 600 && cs->mask > UINT_MAX)
			sec = 600;

		clocks_calc_mult_shift(&cs->mult, &cs->shift, freq,
				       NSEC_PER_SEC / scale, sec * scale);
	}

	if (scale && freq && !cs->uncertainty_margin) {
		cs->uncertainty_margin = NSEC_PER_SEC / (scale * freq);
		if (cs->uncertainty_margin < 2 * 100 * NSEC_PER_USEC)
			cs->uncertainty_margin = 2 * 100 * NSEC_PER_USEC;
	} else if (!cs->uncertainty_margin) {
		cs->uncertainty_margin = NSEC_PER_SEC >> 5;
	}

	cs->maxadj = clocksource_max_adjustment(cs);
	while (freq && ((cs->mult + cs->maxadj < cs->mult) ||
			(cs->mult - cs->maxadj > cs->mult))) {
		cs->mult >>= 1;
		cs->shift--;
		cs->maxadj = clocksource_max_adjustment(cs);
	}

	/* Inlined clocksource_update_max_deferment */
	/* max_idle_ns assignment removed - field was write-only */
	clocks_calc_max_nsecs(cs->mult, cs->shift, cs->maxadj, cs->mask,
			      &cs->max_cycles);
}

int __clocksource_register_scale(struct clocksource *cs, u32 scale, u32 freq)
{
	unsigned long flags;

	clocksource_arch_init(cs);

	if (WARN_ON_ONCE((unsigned int)cs->id >= CSID_MAX))
		cs->id = CSID_GENERIC;
	if (cs->vdso_clock_mode < 0 ||
	    cs->vdso_clock_mode >= VDSO_CLOCKMODE_MAX) {
		cs->vdso_clock_mode = VDSO_CLOCKMODE_NONE;
	}

	__clocksource_update_freq_scale(cs, scale, freq);

	mutex_lock(&clocksource_mutex);

	clocksource_watchdog_lock(&flags);
	/* clocksource_enqueue inlined */
	{
		struct list_head *entry = &clocksource_list;
		struct clocksource *tmp;
		list_for_each_entry(tmp, &clocksource_list, list) {
			if (tmp->rating < cs->rating)
				break;
			entry = &tmp->list;
		}
		list_add(&cs->list, entry);
	}
	/* Inlined clocksource_enqueue_watchdog - wd_list removed */
	if (cs->flags & CLOCK_SOURCE_IS_CONTINUOUS)
		cs->flags |= CLOCK_SOURCE_VALID_FOR_HRES;
	clocksource_watchdog_unlock(&flags);

	__clocksource_select(false);
	mutex_unlock(&clocksource_mutex);
	return 0;
}

/* __clocksource_change_rating removed - unused in minimal kernel */

int clocksource_unregister(struct clocksource *cs)
{
	int ret = 0;

	mutex_lock(&clocksource_mutex);
	if (!list_empty(&cs->list)) {
		unsigned long flags;

		if (cs == curr_clocksource) {
			__clocksource_select(true);
			if (curr_clocksource == cs)
				ret = -EBUSY;
		}

		if (!ret) {
			clocksource_watchdog_lock(&flags);
			list_del_init(&cs->list);
			clocksource_watchdog_unlock(&flags);
		}
	}
	mutex_unlock(&clocksource_mutex);
	return ret;
}
