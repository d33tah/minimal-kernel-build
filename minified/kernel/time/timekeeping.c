
#include <linux/timekeeper_internal.h>
#include <linux/seqlock.h>
#include <linux/stop_machine.h>

#include "timekeeping.h"
#include "ntp_internal.h"
#include "timekeeping_internal.h"

/*
 * Only CLOCK_REALTIME is read in this kernel (ktime_get_real_seconds for tty
 * timestamps, ktime_get_coarse_real_ts64 for inode times), so the timekeeper
 * keeps no monotonic/raw bases or wall-to-monotonic offset.
 */
#define TK_CLEAR_NTP (1 << 0)
#define TK_MIRROR (1 << 1)

DEFINE_RAW_SPINLOCK(timekeeper_lock);

static struct {
	seqcount_raw_spinlock_t seq;
	struct timekeeper timekeeper;
} tk_core ____cacheline_aligned = {
	.seq = SEQCNT_RAW_SPINLOCK_ZERO(tk_core.seq, &timekeeper_lock),
};

static struct timekeeper shadow_timekeeper;

static inline void tk_normalize_xtime(struct timekeeper *tk)
{
	while (tk->tkr_mono.xtime_nsec >=
	       ((u64)NSEC_PER_SEC << tk->tkr_mono.shift)) {
		tk->tkr_mono.xtime_nsec -= (u64)NSEC_PER_SEC
					   << tk->tkr_mono.shift;
		tk->xtime_sec++;
	}
}

static inline struct timespec64 tk_xtime(const struct timekeeper *tk)
{
	struct timespec64 ts;

	ts.tv_sec = tk->xtime_sec;
	ts.tv_nsec = (long)(tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift);
	return ts;
}

static void tk_set_xtime(struct timekeeper *tk, const struct timespec64 *ts)
{
	tk->xtime_sec = ts->tv_sec;
	tk->tkr_mono.xtime_nsec = (u64)ts->tv_nsec << tk->tkr_mono.shift;
}

static inline u64 tk_clock_read(const struct tk_read_base *tkr)
{
	struct clocksource *clock = READ_ONCE(tkr->clock);

	return clock->read(clock);
}

static void tk_setup_internals(struct timekeeper *tk, struct clocksource *clock)
{
	u64 interval;
	u64 tmp, ntpinterval;
	struct clocksource *old_clock;

	old_clock = tk->tkr_mono.clock;
	tk->tkr_mono.clock = clock;
	tk->tkr_mono.mask = clock->mask;
	tk->tkr_mono.cycle_last = tk_clock_read(&tk->tkr_mono);

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

	if (old_clock) {
		int shift_change = clock->shift - old_clock->shift;
		if (shift_change < 0)
			tk->tkr_mono.xtime_nsec >>= -shift_change;
		else
			tk->tkr_mono.xtime_nsec <<= shift_change;
	}

	tk->tkr_mono.shift = clock->shift;

	tk->ntp_error = 0;
	tk->ntp_error_shift = NTP_SCALE_SHIFT - clock->shift;
	tk->ntp_tick = ntpinterval << tk->ntp_error_shift;

	tk->tkr_mono.mult = clock->mult;
	tk->ntp_err_mult = 0;
	tk->skip_second_overflow = 0;
}

static void timekeeping_update(struct timekeeper *tk, unsigned int action)
{
	if (action & TK_CLEAR_NTP)
		tk->ntp_error = 0;

	if (action & TK_MIRROR)
		memcpy(&shadow_timekeeper, &tk_core.timekeeper,
		       sizeof(tk_core.timekeeper));
}

static void timekeeping_forward_now(struct timekeeper *tk)
{
	u64 cycle_now, delta;

	cycle_now = tk_clock_read(&tk->tkr_mono);
	delta = clocksource_delta(cycle_now, tk->tkr_mono.cycle_last,
				  tk->tkr_mono.mask);
	tk->tkr_mono.cycle_last = cycle_now;

	tk->tkr_mono.xtime_nsec += delta * tk->tkr_mono.mult;

	tk_normalize_xtime(tk);
}

/* ktime_get_real_ts64 removed - only caller was gettimeofday (now stubbed) */

/* Removed: ktime_get_with_offset + offsets[], ktime_get_coarse_with_offset, ktime_mono_to_any, ktime_get_raw - no callers */

static int change_clocksource(void *data)
{
	struct timekeeper *tk = &tk_core.timekeeper;
	struct clocksource *new;
	unsigned long flags;
	bool change = false;

	new = (struct clocksource *)data;

	if (!new->enable || new->enable(new) == 0)
		change = true;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&tk_core.seq);

	timekeeping_forward_now(tk);

	if (change)
		tk_setup_internals(tk, new);

	timekeeping_update(tk, TK_CLEAR_NTP | TK_MIRROR);

	write_seqcount_end(&tk_core.seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	return 0;
}

int timekeeping_notify(struct clocksource *clock)
{
	struct timekeeper *tk = &tk_core.timekeeper;

	if (tk->tkr_mono.clock == clock)
		return 0;
	stop_machine(change_clocksource, clock, NULL);
	return tk->tkr_mono.clock == clock ? 0 : -1;
}

void __weak read_persistent_clock64(struct timespec64 *ts)
{
	ts->tv_sec = 0;
	ts->tv_nsec = 0;
}

void __init timekeeping_init(void)
{
	struct timespec64 wall_time;
	struct timekeeper *tk = &tk_core.timekeeper;
	struct clocksource *clock;
	unsigned long flags;

	read_persistent_clock64(&wall_time);
	if (!(timespec64_valid_settod(&wall_time) &&
	      timespec64_to_ns(&wall_time) > 0) &&
	    timespec64_to_ns(&wall_time) != 0) {
		pr_warn("Persistent clock returned invalid value");
		wall_time = (struct timespec64){ 0 };
	}

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&tk_core.seq);

	clock = clocksource_default_clock();
	if (clock->enable)
		clock->enable(clock);
	tk_setup_internals(tk, clock);

	tk_set_xtime(tk, &wall_time);

	timekeeping_update(tk, TK_MIRROR);

	write_seqcount_end(&tk_core.seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);
}

static __always_inline void
timekeeping_apply_adjustment(struct timekeeper *tk, s64 offset, s32 mult_adj)
{
	s64 interval = tk->cycle_interval;

	if (mult_adj == 0) {
		return;
	} else if (mult_adj == -1) {
		interval = -interval;
		offset = -offset;
	} else if (mult_adj != 1) {
		interval *= mult_adj;
		offset *= mult_adj;
	}

	if ((mult_adj > 0) && (tk->tkr_mono.mult + mult_adj < mult_adj)) {
		WARN_ON_ONCE(1);
		return;
	}

	tk->tkr_mono.mult += mult_adj;
	tk->xtime_interval += interval;
	tk->tkr_mono.xtime_nsec -= offset;
}

static void timekeeping_adjust(struct timekeeper *tk, s64 offset)
{
	u32 mult;

	if (likely(tk->ntp_tick == ntp_tick_length())) {
		mult = tk->tkr_mono.mult - tk->ntp_err_mult;
	} else {
		tk->ntp_tick = ntp_tick_length();
		mult = div64_u64((tk->ntp_tick >> tk->ntp_error_shift) -
					 tk->xtime_remainder,
				 tk->cycle_interval);
	}

	tk->ntp_err_mult = tk->ntp_error > 0 ? 1 : 0;
	mult += tk->ntp_err_mult;

	timekeeping_apply_adjustment(tk, offset, mult - tk->tkr_mono.mult);

	if (unlikely((s64)tk->tkr_mono.xtime_nsec < 0)) {
		tk->tkr_mono.xtime_nsec += (u64)NSEC_PER_SEC
					   << tk->tkr_mono.shift;
		tk->xtime_sec--;
		tk->skip_second_overflow = 1;
	}
}

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

		/*
		 * Leap-second handling removed: second_overflow() is a
		 * permanent return-0 stub on this build (NTP gutted), so the
		 * leap correction never fired.
		 */
	}
	return clock_set;
}

static u64 logarithmic_accumulation(struct timekeeper *tk, u64 offset,
				    u32 shift, unsigned int *clock_set)
{
	u64 interval = tk->cycle_interval << shift;

	if (offset < interval)
		return offset;

	offset -= interval;
	tk->tkr_mono.cycle_last += interval;

	tk->tkr_mono.xtime_nsec += tk->xtime_interval << shift;
	*clock_set |= accumulate_nsecs_to_secs(tk);

	tk->ntp_error += tk->ntp_tick << shift;
	tk->ntp_error -= (tk->xtime_interval + tk->xtime_remainder)
			 << (tk->ntp_error_shift + shift);

	return offset;
}

static bool timekeeping_advance(void)
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

	if (offset < real_tk->cycle_interval)
		goto out;

	shift = ilog2(offset) - ilog2(tk->cycle_interval);
	shift = max(0, shift);

	maxshift = (64 - (ilog2(ntp_tick_length()) + 1)) - 1;
	shift = min(shift, maxshift);
	while (offset >= tk->cycle_interval) {
		offset =
			logarithmic_accumulation(tk, offset, shift, &clock_set);
		if (offset < tk->cycle_interval << shift)
			shift--;
	}

	timekeeping_adjust(tk, offset);

	clock_set |= accumulate_nsecs_to_secs(tk);

	write_seqcount_begin(&tk_core.seq);

	timekeeping_update(tk, clock_set);
	memcpy(real_tk, tk, sizeof(*tk));

	write_seqcount_end(&tk_core.seq);
out:
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	return !!clock_set;
}

void update_wall_time(void)
{
	timekeeping_advance();
}

void ktime_get_coarse_real_ts64(struct timespec64 *ts)
{
	struct timekeeper *tk = &tk_core.timekeeper;
	unsigned int seq;

	do {
		seq = read_seqcount_begin(&tk_core.seq);

		*ts = tk_xtime(tk);
	} while (read_seqcount_retry(&tk_core.seq, seq));
}

void do_timer(unsigned long ticks)
{
	jiffies_64 += ticks;
}
