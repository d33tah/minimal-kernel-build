
#include <linux/timekeeper_internal.h>
#include <linux/module.h>
/* linux/interrupt.h removed - no interrupt features used */
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
/* loadavg.h include removed - LOAD_FREQ not used here */
#include <linux/sched/clock.h>
#include <linux/clocksource.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/timex.h>
/* linux/stop_machine.h removed - unused */

#include <linux/compiler.h>

#include "tick-internal.h"
/* ntp.c functions inlined - ntp_init/ntp_clear are empty, others return constants */
#define NTP_TICK_LENGTH ((u64)TICK_NSEC << 32)
#include "timekeeping_internal.h"

#define TK_CLEAR_NTP (1 << 0)
#define TK_MIRROR (1 << 1)
#define TK_CLOCK_WAS_SET (1 << 2)

enum timekeeping_adv_mode {

	TK_ADV_TICK,

	TK_ADV_FREQ
};

DEFINE_RAW_SPINLOCK(timekeeper_lock);

static struct {
	seqcount_raw_spinlock_t seq;
	struct timekeeper timekeeper;
} tk_core ____cacheline_aligned = {
	.seq = SEQCNT_RAW_SPINLOCK_ZERO(tk_core.seq, &timekeeper_lock),
};

static struct timekeeper shadow_timekeeper;

/* timekeeping_suspended removed - never set, always 0 */

/* struct tk_fast, cycles_at_suspend, dummy_clock_read, dummy_clock, FAST_TK_INIT,
   tk_fast_mono, tk_fast_raw removed - fast timekeeper path is stubbed out */
/* tk_normalize_xtime inlined into change_clocksource */
/* tk_xtime, tk_set_xtime, tk_set_wall_to_mono removed - never called */

static inline u64 tk_clock_read(const struct tk_read_base *tkr)
{
	struct clocksource *clock = READ_ONCE(tkr->clock);

	return clock->read(clock);
}

/* timekeeping_get_delta inlined into ktime_get (via timekeeping_get_ns) */

static void tk_setup_internals(struct timekeeper *tk, struct clocksource *clock)
{
	u64 interval;
	u64 tmp, ntpinterval;
	struct clocksource *old_clock;

	++tk->cs_was_changed_seq;
	old_clock = tk->tkr_mono.clock;
	tk->tkr_mono.clock = clock;
	tk->tkr_mono.mask = clock->mask;
	tk->tkr_mono.cycle_last = tk_clock_read(&tk->tkr_mono);

	tk->tkr_raw.clock = clock;
	tk->tkr_raw.mask = clock->mask;
	tk->tkr_raw.cycle_last = tk->tkr_mono.cycle_last;

	tmp = NTP_INTERVAL_LENGTH;
	tmp <<= clock->shift;
	ntpinterval = tmp;
	tmp += clock->mult / 2;
	do_div(tmp, clock->mult);
	if (tmp == 0)
		tmp = 1;

	interval = (u64)tmp;
	tk->cycle_interval = interval;

	tk->xtime_interval = interval * clock->mult;
	tk->xtime_remainder = ntpinterval - tk->xtime_interval;
	tk->raw_interval = interval * clock->mult;

	if (old_clock) {
		int shift_change = clock->shift - old_clock->shift;
		if (shift_change < 0) {
			tk->tkr_mono.xtime_nsec >>= -shift_change;
			tk->tkr_raw.xtime_nsec >>= -shift_change;
		} else {
			tk->tkr_mono.xtime_nsec <<= shift_change;
			tk->tkr_raw.xtime_nsec <<= shift_change;
		}
	}

	tk->tkr_mono.shift = clock->shift;
	tk->tkr_raw.shift = clock->shift;

	tk->ntp_error = 0;
	tk->ntp_error_shift = NTP_SCALE_SHIFT - clock->shift;
	tk->ntp_tick = ntpinterval << tk->ntp_error_shift;

	tk->tkr_mono.mult = clock->mult;
	tk->tkr_raw.mult = clock->mult;
	tk->ntp_err_mult = 0;
	tk->skip_second_overflow = 0;
}

/* timekeeping_delta_to_ns, timekeeping_get_ns inlined into ktime_get */

/* update_fast_timekeeper removed - fast path functions are stubbed */

/* Removed: ktime_get_boot_fast_ns, ktime_get_tai_fast_ns, ktime_get_real_fast_ns,
   ktime_get_fast_timestamps - no callers */

/* pvclock_gtod_chain removed - no registrations */
/* tk_update_ktime_data inlined into timekeeping_update */

static void timekeeping_update(struct timekeeper *tk, unsigned int action)
{
	if (action & TK_CLEAR_NTP) {
		tk->ntp_error = 0;
		/* ntp_clear removed - was empty stub */
	}

	tk->next_leap_ktime = KTIME_MAX; /* tk_update_leap_state inlined */
	/* tk_update_ktime_data inlined */
	{
		u64 seconds;
		u32 nsec;

		seconds = (u64)(tk->xtime_sec + tk->wall_to_monotonic.tv_sec);
		nsec = (u32)tk->wall_to_monotonic.tv_nsec;
		tk->tkr_mono.base = ns_to_ktime(seconds * NSEC_PER_SEC + nsec);

		nsec += (u32)(tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift);
		if (nsec >= NSEC_PER_SEC)
			seconds++;
		tk->ktime_sec = seconds;

		tk->tkr_raw.base = ns_to_ktime(tk->raw_sec * NSEC_PER_SEC);
	}

	/* update_vsyscall, raw_notifier_call_chain(pvclock_gtod_chain),
	   update_fast_timekeeper calls removed - empty stubs */

	tk->tkr_mono.base_real = tk->tkr_mono.base + tk->offs_real;

	if (action & TK_CLOCK_WAS_SET)
		tk->clock_was_set_seq++;

	if (action & TK_MIRROR)
		memcpy(&shadow_timekeeper, &tk_core.timekeeper,
		       sizeof(tk_core.timekeeper));
}

/* timekeeping_forward_now inlined into change_clocksource */

/* ktime_get_real_ts64 removed - never called */

ktime_t ktime_get(void)
{
	struct timekeeper *tk = &tk_core.timekeeper;
	unsigned int seq;
	ktime_t base;
	u64 nsecs;

	/* WARN_ON(timekeeping_suspended) removed - never suspended */

	do {
		seq = read_seqcount_begin(&tk_core.seq);
		base = tk->tkr_mono.base;
		/* timekeeping_get_ns, timekeeping_get_delta, timekeeping_delta_to_ns inlined */
		{
			const struct tk_read_base *tkr = &tk->tkr_mono;
			u64 delta = clocksource_delta(
				tk_clock_read(tkr), tkr->cycle_last, tkr->mask);
			nsecs = (delta * tkr->mult + tkr->xtime_nsec) >>
				tkr->shift;
		}
	} while (read_seqcount_retry(&tk_core.seq, seq));

	return ktime_add_ns(base, nsecs);
}

/* ktime_get_resolution_ns removed - never called (~15 LOC) */

/* offsets array removed - only used by ktime_get_with_offset */
/* ktime_get_with_offset removed - never called (~17 LOC) */

/* Removed: ktime_get_coarse_with_offset, ktime_mono_to_any, ktime_get_raw - no callers */

/* ktime_get_ts64 removed - never called */

/* ktime_get_seconds, ktime_get_real_seconds removed - never called (~20 LOC) */

/* __timekeeping_set_tai_offset removed - unused after second_overflow removal */
/* timekeeping_notify removed - stub inlined into clocksource_select */
/* timekeeping_valid_for_hres removed - never called */
/* read_persistent_clock64 provided by arch/x86/kernel/rtc.c */
/* read_persistent_wall_and_boot_offset removed - never called */

void __init timekeeping_init(void)
{
	/* Minimal init for Hello World - just setup basic jiffies clock */
	struct timekeeper *tk = &tk_core.timekeeper;
	struct clocksource *clock;
	unsigned long flags;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&tk_core.seq);

	clock = clocksource_default_clock();
	tk_setup_internals(tk, clock);

	write_seqcount_end(&tk_core.seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);
}

/* timekeeping_apply_adjustment inlined into timekeeping_adjust, which is inlined into timekeeping_advance */

static inline unsigned int accumulate_nsecs_to_secs(struct timekeeper *tk)
{
	u64 nsecps = (u64)NSEC_PER_SEC << tk->tkr_mono.shift;
	unsigned int clock_set = 0;

	while (tk->tkr_mono.xtime_nsec >= nsecps) {
		tk->tkr_mono.xtime_nsec -= nsecps;
		tk->xtime_sec++;

		if (unlikely(tk->skip_second_overflow)) {
			tk->skip_second_overflow = 0;
			continue;
		}
		/* second_overflow removed - always returned 0, no leap handling */
	}
	return clock_set;
}

/* logarithmic_accumulation inlined into timekeeping_advance */

static bool timekeeping_advance(enum timekeeping_adv_mode mode)
{
	struct timekeeper *real_tk = &tk_core.timekeeper;
	struct timekeeper *tk = &shadow_timekeeper;
	u64 offset;
	int shift = 0, maxshift;
	unsigned int clock_set = 0;
	unsigned long flags;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);

	offset = clocksource_delta(tk_clock_read(&tk->tkr_mono),
				   tk->tkr_mono.cycle_last, tk->tkr_mono.mask);

	if (offset < real_tk->cycle_interval && mode == TK_ADV_TICK)
		goto out;

	shift = ilog2(offset) - ilog2(tk->cycle_interval);
	shift = max(0, shift);

	maxshift = (64 - (ilog2(NTP_TICK_LENGTH) + 1)) - 1;
	shift = min(shift, maxshift);
	while (offset >= tk->cycle_interval) {
		/* logarithmic_accumulation inlined */
		u64 interval = tk->cycle_interval << shift;
		u64 snsec_per_sec;

		if (offset < interval)
			break;

		offset -= interval;
		tk->tkr_mono.cycle_last += interval;
		tk->tkr_raw.cycle_last += interval;

		tk->tkr_mono.xtime_nsec += tk->xtime_interval << shift;
		clock_set |= accumulate_nsecs_to_secs(tk);

		tk->tkr_raw.xtime_nsec += tk->raw_interval << shift;
		snsec_per_sec = (u64)NSEC_PER_SEC << tk->tkr_raw.shift;
		while (tk->tkr_raw.xtime_nsec >= snsec_per_sec) {
			tk->tkr_raw.xtime_nsec -= snsec_per_sec;
			tk->raw_sec++;
		}

		tk->ntp_error += tk->ntp_tick << shift;
		tk->ntp_error -= (tk->xtime_interval + tk->xtime_remainder)
				 << (tk->ntp_error_shift + shift);

		if (offset < tk->cycle_interval << shift)
			shift--;
	}

	/* Inlined timekeeping_adjust */
	{
		u32 mult;
		s32 mult_adj;
		s64 interval;

		if (likely(tk->ntp_tick == NTP_TICK_LENGTH)) {
			mult = tk->tkr_mono.mult - tk->ntp_err_mult;
		} else {
			tk->ntp_tick = NTP_TICK_LENGTH;
			mult = div64_u64((tk->ntp_tick >> tk->ntp_error_shift) -
						 tk->xtime_remainder,
					 tk->cycle_interval);
		}

		tk->ntp_err_mult = tk->ntp_error > 0 ? 1 : 0;
		mult += tk->ntp_err_mult;

		mult_adj = mult - tk->tkr_mono.mult;
		interval = tk->cycle_interval;

		if (mult_adj != 0) {
			if (mult_adj == -1) {
				interval = -interval;
				offset = -offset;
			} else if (mult_adj != 1) {
				interval *= mult_adj;
				offset *= mult_adj;
			}

			if (!((mult_adj > 0) &&
			      (tk->tkr_mono.mult + mult_adj < mult_adj))) {
				tk->tkr_mono.mult += mult_adj;
				tk->xtime_interval += interval;
				tk->tkr_mono.xtime_nsec -= offset;
			} else {
				WARN_ON_ONCE(1);
			}
		}

		if (unlikely(
			    tk->tkr_mono.clock->maxadj &&
			    (abs(tk->tkr_mono.mult - tk->tkr_mono.clock->mult) >
			     tk->tkr_mono.clock->maxadj))) {
			printk_once(
				KERN_WARNING
				"Adjusting %s more than 11%% (%ld vs %ld)\n",
				tk->tkr_mono.clock->name,
				(long)tk->tkr_mono.mult,
				(long)tk->tkr_mono.clock->mult +
					tk->tkr_mono.clock->maxadj);
		}

		if (unlikely((s64)tk->tkr_mono.xtime_nsec < 0)) {
			tk->tkr_mono.xtime_nsec += (u64)NSEC_PER_SEC
						   << tk->tkr_mono.shift;
			tk->xtime_sec--;
			tk->skip_second_overflow = 1;
		}
	}

	clock_set |= accumulate_nsecs_to_secs(tk);

	write_seqcount_begin(&tk_core.seq);

	timekeeping_update(tk, clock_set);
	memcpy(real_tk, tk, sizeof(*tk));

	write_seqcount_end(&tk_core.seq);
out:
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	return !!clock_set;
}

/* update_wall_time removed - never called (~5 LOC) */
/* ktime_get_coarse_real_ts64 removed - never called (~11 LOC) */

/* do_timer removed - never called (~5 LOC) */

/* ktime_get_update_offsets_now removed - never called (~28 LOC) */
/* random_get_entropy_fallback removed - random_get_entropy() is never called (~8 LOC) */
