#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched/clock.h>
#include <linux/delay.h>
#include <linux/clocksource.h>

#include <asm/timer.h>
#include <asm/i8259.h>

unsigned int __read_mostly cpu_khz;

unsigned int __read_mostly tsc_khz;

#define KHZ 1000

static DEFINE_STATIC_KEY_FALSE(__use_tsc);

/* art_to_tsc_numerator, art_to_tsc_denominator, art_to_tsc_offset,
   art_related_clocksource removed - unused after convert_art_to_tsc removal */

struct cyc2ns {
	struct cyc2ns_data data[2];
	seqcount_latch_t seq;
};

static DEFINE_PER_CPU_ALIGNED(struct cyc2ns, cyc2ns);

static __always_inline unsigned long long cycles_2_ns(unsigned long long cyc)
{
	struct cyc2ns_data data;
	unsigned long long ns;
	int seq, idx;

	/* folded sole callers cyc2ns_read_begin()/cyc2ns_read_end() inline */
	preempt_disable_notrace();

	do {
		seq = this_cpu_read(cyc2ns.seq.seqcount.sequence);
		idx = seq & 1;

		data.cyc2ns_offset =
			this_cpu_read(cyc2ns.data[idx].cyc2ns_offset);
		data.cyc2ns_mul = this_cpu_read(cyc2ns.data[idx].cyc2ns_mul);
		data.cyc2ns_shift =
			this_cpu_read(cyc2ns.data[idx].cyc2ns_shift);

	} while (unlikely(seq != this_cpu_read(cyc2ns.seq.seqcount.sequence)));

	ns = data.cyc2ns_offset;
	ns += mul_u64_u32_shr(cyc, data.cyc2ns_mul, data.cyc2ns_shift);

	preempt_enable_notrace();

	return ns;
}

static void __set_cyc2ns_scale(unsigned long khz, int cpu,
			       unsigned long long tsc_now)
{
	unsigned long long ns_now;
	struct cyc2ns_data data;
	struct cyc2ns *c2n;

	ns_now = cycles_2_ns(tsc_now);

	clocks_calc_mult_shift(&data.cyc2ns_mul, &data.cyc2ns_shift, khz,
			       NSEC_PER_MSEC, 0);

	if (data.cyc2ns_shift == 32) {
		data.cyc2ns_shift = 31;
		data.cyc2ns_mul >>= 1;
	}

	data.cyc2ns_offset = ns_now - mul_u64_u32_shr(tsc_now, data.cyc2ns_mul,
						      data.cyc2ns_shift);

	c2n = per_cpu_ptr(&cyc2ns, cpu);

	raw_write_seqcount_latch(&c2n->seq);
	c2n->data[0] = data;
	raw_write_seqcount_latch(&c2n->seq);
	c2n->data[1] = data;
}

/*
 * cyc2ns_init_secondary_cpus() removed: NR_CPUS=1 / SMP off, so there are
 * no secondary CPUs. Its for_each_possible_cpu() loop only ever visited the
 * boot CPU (cpu == this_cpu), whose per-CPU cyc2ns was already set up in
 * tsc_enable_sched_clock(); the cpu != this_cpu body never ran.
 */

u64 native_sched_clock(void)
{
	if (static_branch_likely(&__use_tsc)) {
		u64 tsc_now = rdtsc();

		return cycles_2_ns(tsc_now);
	}

	return (jiffies_64 - INITIAL_JIFFIES) * (1000000000 / HZ);
}

unsigned long long sched_clock(void)
	__attribute__((alias("native_sched_clock")));

static inline int pit_verify_msb(unsigned char val)
{
	inb(0x42);
	return inb(0x42) == val;
}

static inline int pit_expect_msb(unsigned char val, u64 *tscp,
				 unsigned long *deltap)
{
	int count;
	u64 tsc = 0, prev_tsc = 0;

	for (count = 0; count < 50000; count++) {
		if (!pit_verify_msb(val))
			break;
		prev_tsc = tsc;
		tsc = get_cycles();
	}
	*deltap = get_cycles() - prev_tsc;
	*tscp = tsc;

	return count > 5;
}

#define MAX_QUICK_PIT_MS 50
#define MAX_QUICK_PIT_ITERATIONS (MAX_QUICK_PIT_MS * PIT_TICK_RATE / 1000 / 256)

static unsigned long quick_pit_calibrate(void)
{
	int i;
	u64 tsc, delta;
	unsigned long d1, d2;

	if (!has_legacy_pic())
		return 0;

	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	outb(0xb0, 0x43);

	outb(0xff, 0x42);
	outb(0xff, 0x42);

	pit_verify_msb(0);

	if (pit_expect_msb(0xff, &tsc, &d1)) {
		for (i = 1; i <= MAX_QUICK_PIT_ITERATIONS; i++) {
			if (!pit_expect_msb(0xff - i, &delta, &d2))
				break;

			delta -= tsc;

			if (i == 1 &&
			    d1 + d2 >= (delta * MAX_QUICK_PIT_ITERATIONS) >> 11)
				return 0;

			if (d1 + d2 >= delta >> 11)
				continue;

			if (!pit_verify_msb(0xfe - i))
				break;
			goto success;
		}
	}
	pr_info("Fast TSC calibration failed\n");
	return 0;

success:

	delta *= PIT_TICK_RATE;
	do_div(delta, i * 256 * 1000);
	pr_info("Fast TSC calibration using PIT\n");
	return delta;
}

/*
 * The CPU (and TSC) frequency is measured against the PIT; there is no
 * vendor-specific CPUID 0x15/0x16 frequency enumeration.
 */
unsigned long native_calibrate_cpu_early(void)
{
	unsigned long flags, fast_calibrate;

	local_irq_save(flags);
	fast_calibrate = quick_pit_calibrate();
	local_irq_restore(flags);
	return fast_calibrate;
}

static u64 read_tsc(struct clocksource *cs)
{
	return (u64)rdtsc_ordered();
}

static struct clocksource clocksource_tsc = {
	.name = "tsc",
	.rating = 300,
	.read = read_tsc,
	.mask = CLOCKSOURCE_MASK(64),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	.list = LIST_HEAD_INIT(clocksource_tsc.list),
};

static bool __init determine_cpu_tsc_frequencies(bool early)
{
	WARN_ON(cpu_khz || tsc_khz);

	/* Only the early CPUID-based calibration path survives on this build. */
	cpu_khz = native_calibrate_cpu_early();
	tsc_khz = cpu_khz;

	if (tsc_khz == 0)
		return false;

	pr_info("Detected %lu.%03lu MHz processor\n",
		(unsigned long)cpu_khz / KHZ, (unsigned long)cpu_khz % KHZ);
	return true;
}

static void __init tsc_enable_sched_clock(void)
{
	struct cyc2ns *c2n = this_cpu_ptr(&cyc2ns);
	u64 lpj = (u64)tsc_khz * KHZ;

	do_div(lpj, HZ);
	loops_per_jiffy = lpj;

	seqcount_latch_init(&c2n->seq);
	__set_cyc2ns_scale(tsc_khz, smp_processor_id(), rdtsc());
	static_branch_enable(&__use_tsc);
}

void __init tsc_early_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_TSC))
		return;

	if (!determine_cpu_tsc_frequencies(true))
		return;
	tsc_enable_sched_clock();
}

void __init tsc_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_TSC))
		return;

	/* PIT calibration can fail under host load, leaving tsc_khz == 0. */
	if (!tsc_khz)
		return;

	/*
	 * Registered here, synchronously. The old two-stage scheme registered
	 * "tsc-early" here and "tsc" from a delayed work item that never ran.
	 */
	clocksource_register_khz(&clocksource_tsc, tsc_khz);
}
