/* Stub: Clocksource - minimal for single-CPU hello-world kernel */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <linux/kthread.h>
#include <linux/cpu.h>

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
static char override_name[CS_NAME_LEN];
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

void clocksource_mark_unstable(struct clocksource *cs)
{
}

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
	max_nsecs = clocksource_cyc2ns(max_cycles, mult - maxadj, shift);

	if (max_cyc)
		*max_cyc = max_cycles;

	max_nsecs >>= 1;

	return max_nsecs;
}

static inline void clocksource_update_max_deferment(struct clocksource *cs)
{
	cs->max_idle_ns = clocks_calc_max_nsecs(cs->mult, cs->shift, cs->maxadj,
						cs->mask, &cs->max_cycles);
}

static struct clocksource *clocksource_find_best(bool oneshot, bool skipcur)
{
	struct clocksource *cs;

	if (!finished_booting || list_empty(&clocksource_list))
		return NULL;

	list_for_each_entry(cs, &clocksource_list, list) {
		if (skipcur && cs == curr_clocksource)
			continue;
		if (oneshot && !(cs->flags & CLOCK_SOURCE_VALID_FOR_HRES))
			continue;
		return cs;
	}
	return NULL;
}

static void __clocksource_select(bool skipcur)
{
	bool oneshot = tick_oneshot_mode_active();
	struct clocksource *best, *cs;

	best = clocksource_find_best(oneshot, skipcur);
	if (!best)
		return;

	if (!strlen(override_name))
		goto found;

	list_for_each_entry(cs, &clocksource_list, list) {
		if (skipcur && cs == curr_clocksource)
			continue;
		if (strcmp(cs->name, override_name) != 0)
			continue;
		if (!(cs->flags & CLOCK_SOURCE_VALID_FOR_HRES) && oneshot) {
			if (cs->flags & CLOCK_SOURCE_UNSTABLE) {
				override_name[0] = 0;
			}
		} else
			best = cs;
		break;
	}

found:
	if (curr_clocksource != best && !timekeeping_notify(best)) {
		curr_clocksource = best;
	}
}

static void clocksource_select(void)
{
	__clocksource_select(false);
}

static void clocksource_select_fallback(void)
{
	__clocksource_select(true);
}

static int __init clocksource_done_booting(void)
{
	mutex_lock(&clocksource_mutex);
	curr_clocksource = clocksource_default_clock();
	finished_booting = 1;
	clocksource_select();
	mutex_unlock(&clocksource_mutex);
	return 0;
}
fs_initcall(clocksource_done_booting);

static void clocksource_enqueue(struct clocksource *cs)
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

/* Stub: watchdog enqueue - mark continuous sources as valid */
static void clocksource_enqueue_watchdog(struct clocksource *cs)
{
	INIT_LIST_HEAD(&cs->wd_list);
	if (cs->flags & CLOCK_SOURCE_IS_CONTINUOUS)
		cs->flags |= CLOCK_SOURCE_VALID_FOR_HRES;
}

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

	clocksource_update_max_deferment(cs);
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
	clocksource_enqueue(cs);
	clocksource_enqueue_watchdog(cs);
	clocksource_watchdog_unlock(&flags);

	clocksource_select();
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
			clocksource_select_fallback();
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
