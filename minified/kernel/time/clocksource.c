/* Stub: Clocksource - minimal for single-CPU hello-world kernel */

#include <linux/clocksource.h>
#include <linux/module.h>

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
static int finished_booting;

static void __clocksource_select(bool skipcur)
{
	struct clocksource *best = NULL, *cs;

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

	if (curr_clocksource != best)
		curr_clocksource = best;
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

	{
		u64 adj = (u64)cs->mult * 11;
		do_div(adj, 100);
		cs->maxadj = (u32)adj;
	}
	while (freq && ((cs->mult + cs->maxadj < cs->mult) ||
			(cs->mult - cs->maxadj > cs->mult))) {
		u64 adj;
		cs->mult >>= 1;
		cs->shift--;
		adj = (u64)cs->mult * 11;
		do_div(adj, 100);
		cs->maxadj = (u32)adj;
	}

	{
		u64 max_cycles = ULLONG_MAX;
		do_div(max_cycles, cs->mult + cs->maxadj);
		max_cycles = min(max_cycles, cs->mask);
		cs->max_cycles = max_cycles;
	}
}

int __clocksource_register_scale(struct clocksource *cs, u32 scale, u32 freq)
{
	__clocksource_update_freq_scale(cs, scale, freq);

	mutex_lock(&clocksource_mutex);
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
	if (cs->flags & CLOCK_SOURCE_IS_CONTINUOUS)
		cs->flags |= CLOCK_SOURCE_VALID_FOR_HRES;

	__clocksource_select(false);
	mutex_unlock(&clocksource_mutex);
	return 0;
}
