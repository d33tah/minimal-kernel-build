#include <linux/clocksource.h>
#include <linux/module.h>

extern raw_spinlock_t jiffies_lock;
extern seqcount_raw_spinlock_t jiffies_seq;
/* tick-internal.h inlined */
#if HZ < 34
#define JIFFIES_SHIFT 6
#elif HZ < 67
#define JIFFIES_SHIFT 7
#else
#define JIFFIES_SHIFT 8
#endif

static u64 jiffies_read(struct clocksource *cs)
{
	return (u64)jiffies;
}

static struct clocksource clocksource_jiffies = {
	.name = "jiffies",
	.rating = 1,
	.uncertainty_margin = 32 * NSEC_PER_MSEC,
	.read = jiffies_read,
	.mask = CLOCKSOURCE_MASK(32),
	.mult = TICK_NSEC << JIFFIES_SHIFT,
	.shift = JIFFIES_SHIFT,
	.max_cycles = 10,
};

__cacheline_aligned_in_smp DEFINE_RAW_SPINLOCK(jiffies_lock);
__cacheline_aligned_in_smp seqcount_raw_spinlock_t jiffies_seq =
	SEQCNT_RAW_SPINLOCK_ZERO(jiffies_seq, &jiffies_lock);

static int __init init_jiffies_clocksource(void)
{
	return __clocksource_register(&clocksource_jiffies);
}

core_initcall(init_jiffies_clocksource);

struct clocksource *__init __weak clocksource_default_clock(void)
{
	return &clocksource_jiffies;
}
