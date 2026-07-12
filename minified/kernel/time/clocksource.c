
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clocksource.h>

#include "tick-internal.h"

void
clocks_calc_mult_shift(u32 *mult, u32 *shift, u32 from, u32 to, u32 maxsec) {
	u64 tmp;
	u32 sft, sftacc= 32;

	 
	tmp = ((u64)maxsec * from) >> 32;
	while (tmp) {
		tmp >>=1;
		sftacc--; }

	 
	for (sft = 32; sft > 0; sft--) {
		tmp = (u64) to << sft;
		tmp += from / 2;
		do_div(tmp, from);
		if ((tmp >> sftacc) == 0)
			break; }
	*mult = tmp;
	*shift = sft; }

static struct clocksource *curr_clocksource;
static struct clocksource *suspend_clocksource;
static LIST_HEAD(clocksource_list);
static DEFINE_MUTEX(clocksource_mutex);
static int finished_booting;

#define WATCHDOG_THRESHOLD (NSEC_PER_SEC >> 5)

#define MAX_SKEW_USEC	100

#define WATCHDOG_MAX_SKEW (MAX_SKEW_USEC * NSEC_PER_USEC)


/*
 * The clocksource skew watchdog (timer + kthread) is gone: this minimal
 * kernel boots, execs one static init, services write(2)+exit and halts,
 * long before the WATCHDOG_INTERVAL timer would ever fire. Only the few
 * register/unregister hooks remain, reduced to the bookkeeping that the
 * live clocksource-select path needs.
 */

static void clocksource_enqueue_watchdog(struct clocksource *cs) {
	if (!(cs->flags & CLOCK_SOURCE_MUST_VERIFY) && (cs->flags & CLOCK_SOURCE_IS_CONTINUOUS))
		cs->flags |= CLOCK_SOURCE_VALID_FOR_HRES; }

static void __clocksource_suspend_select(struct clocksource *cs) {
	 
	if (!(cs->flags & CLOCK_SOURCE_SUSPEND_NONSTOP))
		return;


	if (!suspend_clocksource || cs->rating > suspend_clocksource->rating)
		suspend_clocksource = cs; }

static u32 clocksource_max_adjustment(struct clocksource *cs) {
	u64 ret;
	 
	ret = (u64)cs->mult * 11;
	do_div(ret,100);
	return (u32)ret; }

static struct clocksource *clocksource_find_best(bool skipcur) {
	struct clocksource *cs;

	if (!finished_booting || list_empty(&clocksource_list))
		return NULL;


	list_for_each_entry(cs, &clocksource_list, list) {
		if (skipcur && cs == curr_clocksource)
			continue;
		return cs; }
	return NULL; }

static void __clocksource_select(bool skipcur) {
	struct clocksource *best;

	/* override_name is never set on this build, so the override-match
	 * selection loop is dead; the highest-rated clocksource always wins.
	 */
	best = clocksource_find_best(skipcur);
	if (!best)
		return;

	if (curr_clocksource != best && !timekeeping_notify(best)) {
		curr_clocksource = best; } }

static void clocksource_select(void) {
	__clocksource_select(false); }

static int __init clocksource_done_booting(void) {
	mutex_lock(&clocksource_mutex);
	curr_clocksource = clocksource_default_clock();
	finished_booting = 1;
	clocksource_select();
	mutex_unlock(&clocksource_mutex);
	return 0; }
fs_initcall(clocksource_done_booting);

static void clocksource_enqueue(struct clocksource *cs) {
	struct list_head *entry = &clocksource_list;
	struct clocksource *tmp;

	list_for_each_entry(tmp, &clocksource_list, list) {
		 
		if (tmp->rating < cs->rating)
			break;
		entry = &tmp->list; }
	list_add(&cs->list, entry); }

void __clocksource_update_freq_scale(struct clocksource *cs, u32 scale, u32 freq) {
	u64 sec;

	 
	if (freq) {
		 
		sec = cs->mask;
		do_div(sec, freq);
		do_div(sec, scale);
		if (!sec)
			sec = 1;
		else if (sec > 600 && cs->mask > UINT_MAX)
			sec = 600;

		clocks_calc_mult_shift(&cs->mult, &cs->shift, freq, NSEC_PER_SEC / scale, sec * scale); }

	 
	if (scale && freq && !cs->uncertainty_margin) {
		cs->uncertainty_margin = NSEC_PER_SEC / (scale * freq);
		if (cs->uncertainty_margin < 2 * WATCHDOG_MAX_SKEW)
			cs->uncertainty_margin = 2 * WATCHDOG_MAX_SKEW;
	} else if (!cs->uncertainty_margin) {
		cs->uncertainty_margin = WATCHDOG_THRESHOLD; }
	WARN_ON_ONCE(cs->uncertainty_margin < 2 * WATCHDOG_MAX_SKEW);

	 
	cs->maxadj = clocksource_max_adjustment(cs);
	while (freq && ((cs->mult + cs->maxadj < cs->mult) || (cs->mult - cs->maxadj > cs->mult))) {
		cs->mult >>= 1;
		cs->shift--;
		cs->maxadj = clocksource_max_adjustment(cs); }

	 
	WARN_ONCE(cs->mult + cs->maxadj < cs->mult, "timekeeping: Clocksource %s might overflow on 11%% adjustment\n", cs->name); }

int __clocksource_register_scale(struct clocksource *cs, u32 scale, u32 freq) {
	clocksource_arch_init(cs);

	if (WARN_ON_ONCE((unsigned int)cs->id >= CSID_MAX))
		cs->id = CSID_GENERIC;
	if (cs->vdso_clock_mode >= VDSO_CLOCKMODE_MAX) {
		pr_warn("clocksource %s registered with invalid VDSO mode %d. Disabling VDSO support.\n", cs->name, cs->vdso_clock_mode);
		cs->vdso_clock_mode = VDSO_CLOCKMODE_NONE; }

	 
	__clocksource_update_freq_scale(cs, scale, freq);

	 
	mutex_lock(&clocksource_mutex);

	clocksource_enqueue(cs);
	clocksource_enqueue_watchdog(cs);

	clocksource_select();
	__clocksource_suspend_select(cs);
	mutex_unlock(&clocksource_mutex);
	return 0; }

int clocksource_unregister(struct clocksource *cs) {
	return 0; }


