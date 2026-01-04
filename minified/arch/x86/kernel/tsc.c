#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/timer.h>
/* Inlined from acpi_pmtmr.h */
#define PMTMR_TICKS_PER_SEC 3579545
#define ACPI_PM_OVRRUN (1 << 24)
static inline u32 acpi_pm_read_early(void)
{
	return 0;
}
#include <linux/delay.h>
#include <linux/clocksource.h>
#include <linux/percpu.h>
#include <linux/timex.h>
#include <linux/jump_label.h>
#include <linux/static_call.h>

/* is_hpet_enabled, hpet_readl, HPET_COUNTER, HPET_PERIOD removed - hpet disabled */
#include <asm/timer.h>
#include <asm/vgtod.h>
#include <asm/time.h>
#include <asm/delay.h>
#include <asm/nmi.h>
#include <asm/x86_init.h>
#include <asm/apic.h>
#include <asm/intel-family.h>
#include <asm/i8259.h>
/* is_early_uv_system removed - always returned false */

unsigned int __read_mostly cpu_khz;

unsigned int __read_mostly tsc_khz;

#define KHZ 1000

static int __read_mostly tsc_unstable;
static unsigned int __initdata tsc_early_khz;

static DEFINE_STATIC_KEY_FALSE(__use_tsc);

int tsc_clocksource_reliable;

/* art_to_tsc_numerator, art_to_tsc_denominator, art_to_tsc_offset,
   art_related_clocksource removed - unused after convert_art_to_tsc removal */

struct cyc2ns {
	struct cyc2ns_data data[2];
	seqcount_latch_t seq;
};

static DEFINE_PER_CPU_ALIGNED(struct cyc2ns, cyc2ns);

__always_inline void cyc2ns_read_begin(struct cyc2ns_data *data)
{
	int seq, idx;

	preempt_disable_notrace();

	do {
		seq = this_cpu_read(cyc2ns.seq.seqcount.sequence);
		idx = seq & 1;

		data->cyc2ns_offset =
			this_cpu_read(cyc2ns.data[idx].cyc2ns_offset);
		data->cyc2ns_mul = this_cpu_read(cyc2ns.data[idx].cyc2ns_mul);
		data->cyc2ns_shift =
			this_cpu_read(cyc2ns.data[idx].cyc2ns_shift);

	} while (unlikely(seq != this_cpu_read(cyc2ns.seq.seqcount.sequence)));
}

__always_inline void cyc2ns_read_end(void)
{
	preempt_enable_notrace();
}

static __always_inline unsigned long long cycles_2_ns(unsigned long long cyc)
{
	struct cyc2ns_data data;
	unsigned long long ns;

	cyc2ns_read_begin(&data);

	ns = data.cyc2ns_offset;
	ns += mul_u64_u32_shr(cyc, data.cyc2ns_mul, data.cyc2ns_shift);

	cyc2ns_read_end();

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

static void __init cyc2ns_init_boot_cpu(void)
{
	struct cyc2ns *c2n = this_cpu_ptr(&cyc2ns);

	seqcount_latch_init(&c2n->seq);
	__set_cyc2ns_scale(tsc_khz, smp_processor_id(), rdtsc());
}

/* cyc2ns_init_secondary_cpus - single CPU, loop body is dead code */
static void __init cyc2ns_init_secondary_cpus(void)
{
	/* NR_CPUS=1, so for_each_possible_cpu only yields cpu=0 which equals
	 * smp_processor_id(), making the (cpu != this_cpu) condition always false */
}

u64 native_sched_clock(void)
{
	if (static_branch_likely(&__use_tsc)) {
		u64 tsc_now = rdtsc();

		return cycles_2_ns(tsc_now);
	}

	return (jiffies_64 - INITIAL_JIFFIES) * (1000000000 / HZ);
}

/* native_sched_clock_from_tsc removed - never called */

unsigned long long sched_clock(void)
	__attribute__((alias("native_sched_clock")));
/* using_native_sched_clock removed - never called (~5 LOC) */

/* no_sched_irq_time removed - never used */
static int no_tsc_watchdog;

#define MAX_RETRIES 5
#define TSC_DEFAULT_THRESHOLD 0x20000

static u64 tsc_read_refs(u64 *p, int hpet)
{
	u64 t1, t2;
	u64 thresh = tsc_khz ? tsc_khz >> 5 : TSC_DEFAULT_THRESHOLD;
	int i;

	/* hpet always 0 - hpet_readl branch removed */
	for (i = 0; i < MAX_RETRIES; i++) {
		t1 = get_cycles();
		*p = acpi_pm_read_early();
		t2 = get_cycles();
		if ((t2 - t1) < thresh)
			return t2;
	}
	return ULLONG_MAX;
}

/* calc_hpet_ref removed - is_hpet_enabled always 0, never called */

static unsigned long calc_pmtimer_ref(u64 deltatsc, u64 pm1, u64 pm2)
{
	u64 tmp;

	if (!pm1 && !pm2)
		return ULONG_MAX;

	if (pm2 < pm1)
		pm2 += (u64)ACPI_PM_OVRRUN;
	pm2 -= pm1;
	tmp = pm2 * 1000000000LL;
	do_div(tmp, PMTMR_TICKS_PER_SEC);
	do_div(deltatsc, tmp);

	return (unsigned long)deltatsc;
}

#define CAL_MS 10
#define CAL_LATCH (PIT_TICK_RATE / (1000 / CAL_MS))
#define CAL_PIT_LOOPS 1000

#define CAL2_MS 50
#define CAL2_LATCH (PIT_TICK_RATE / (1000 / CAL2_MS))
#define CAL2_PIT_LOOPS 5000

static unsigned long pit_calibrate_tsc(u32 latch, unsigned long ms, int loopmin)
{
	u64 tsc, t1, t2, delta;
	unsigned long tscmin, tscmax;
	int pitcnt;

	if (!has_legacy_pic()) {
		udelay(10 * USEC_PER_MSEC);
		udelay(10 * USEC_PER_MSEC);
		udelay(10 * USEC_PER_MSEC);
		udelay(10 * USEC_PER_MSEC);
		udelay(10 * USEC_PER_MSEC);
		return ULONG_MAX;
	}

	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	outb(0xb0, 0x43);
	outb(latch & 0xff, 0x42);
	outb(latch >> 8, 0x42);

	tsc = t1 = t2 = get_cycles();

	pitcnt = 0;
	tscmax = 0;
	tscmin = ULONG_MAX;
	while ((inb(0x61) & 0x20) == 0) {
		t2 = get_cycles();
		delta = t2 - tsc;
		tsc = t2;
		if ((unsigned long)delta < tscmin)
			tscmin = (unsigned int)delta;
		if ((unsigned long)delta > tscmax)
			tscmax = (unsigned int)delta;
		pitcnt++;
	}

	if (pitcnt < loopmin || tscmax > 10 * tscmin)
		return ULONG_MAX;

	delta = t2 - t1;
	do_div(delta, ms);
	return delta;
}

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

unsigned long native_calibrate_tsc(void)
{
	unsigned int eax_denominator, ebx_numerator, ecx_hz, edx;
	unsigned int crystal_khz;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return 0;

	if (boot_cpu_data.cpuid_level < 0x15)
		return 0;

	eax_denominator = ebx_numerator = ecx_hz = edx = 0;

	cpuid(0x15, &eax_denominator, &ebx_numerator, &ecx_hz, &edx);

	if (ebx_numerator == 0 || eax_denominator == 0)
		return 0;

	crystal_khz = ecx_hz / 1000;

	if (crystal_khz == 0 &&
	    boot_cpu_data.x86_model == INTEL_FAM6_ATOM_GOLDMONT_D)
		crystal_khz = 25000;

	if (crystal_khz != 0)
		setup_force_cpu_cap(X86_FEATURE_TSC_KNOWN_FREQ);

	if (crystal_khz == 0 && boot_cpu_data.cpuid_level >= 0x16) {
		unsigned int eax_base_mhz, ebx, ecx, edx;

		cpuid(0x16, &eax_base_mhz, &ebx, &ecx, &edx);
		crystal_khz =
			eax_base_mhz * 1000 * eax_denominator / ebx_numerator;
	}

	if (crystal_khz == 0)
		return 0;

	if (boot_cpu_data.x86_model == INTEL_FAM6_ATOM_GOLDMONT)
		setup_force_cpu_cap(X86_FEATURE_TSC_RELIABLE);

	return crystal_khz * ebx_numerator / eax_denominator;
}

static unsigned long cpu_khz_from_cpuid(void)
{
	unsigned int eax_base_mhz, ebx_max_mhz, ecx_bus_mhz, edx;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return 0;

	if (boot_cpu_data.cpuid_level < 0x16)
		return 0;

	eax_base_mhz = ebx_max_mhz = ecx_bus_mhz = edx = 0;

	cpuid(0x16, &eax_base_mhz, &ebx_max_mhz, &ecx_bus_mhz, &edx);

	return eax_base_mhz * 1000;
}

static unsigned long pit_hpet_ptimer_calibrate_cpu(void)
{
	u64 tsc1, tsc2, delta, ref1, ref2;
	unsigned long tsc_pit_min = ULONG_MAX, tsc_ref_min = ULONG_MAX;
	unsigned long flags, latch, ms;
	int hpet, i, loopmin;

#ifndef CONFIG_MMU
	/* For NOMMU, skip complex calibration and use default ~1GHz */
	{
		const char *s = "pit_calibrate: NOMMU stub, returning 1GHz\n";
		while (*s)
			asm volatile("outb %0, $0xe9" : : "a"(*s++));
	}
	return 1000000; /* 1 GHz in kHz */
#endif

	hpet = 0; /* is_hpet_enabled always 0 */

	latch = CAL_LATCH;
	ms = CAL_MS;
	loopmin = CAL_PIT_LOOPS;

	for (i = 0; i < 3; i++) {
		unsigned long tsc_pit_khz;

		local_irq_save(flags);
		tsc1 = tsc_read_refs(&ref1, hpet);
		tsc_pit_khz = pit_calibrate_tsc(latch, ms, loopmin);
		tsc2 = tsc_read_refs(&ref2, hpet);
		local_irq_restore(flags);

		tsc_pit_min = min(tsc_pit_min, tsc_pit_khz);

		if (ref1 == ref2)
			continue;

		if (tsc1 == ULLONG_MAX || tsc2 == ULLONG_MAX)
			continue;

		tsc2 = (tsc2 - tsc1) * 1000000LL;
		/* hpet always 0, so calc_hpet_ref never called */
		tsc2 = calc_pmtimer_ref(tsc2, ref1, ref2);

		tsc_ref_min = min(tsc_ref_min, (unsigned long)tsc2);

		delta = ((u64)tsc_pit_min) * 100;
		do_div(delta, tsc_ref_min);

		if (delta >= 90 && delta <= 110) {
			pr_info("PIT calibration matches PMTIMER. %d loops\n",
				i + 1);
			return tsc_ref_min;
		}

		if (i == 1 && tsc_pit_min == ULONG_MAX) {
			latch = CAL2_LATCH;
			ms = CAL2_MS;
			loopmin = CAL2_PIT_LOOPS;
		}
	}

	if (tsc_pit_min == ULONG_MAX) {
		pr_warn("Unable to calibrate against PIT\n");

		/* hpet always 0 */
		if (!ref1 && !ref2) {
			pr_notice("No reference (PMTIMER) available\n");
			return 0;
		}

		if (tsc_ref_min == ULONG_MAX) {
			pr_warn("PMTIMER calibration failed\n");
			return 0;
		}

		pr_info("using PMTIMER reference calibration\n");

		return tsc_ref_min;
	}

	if (!hpet && !ref1 && !ref2) {
		pr_info("Using PIT calibration value\n");
		return tsc_pit_min;
	}

	if (tsc_ref_min == ULONG_MAX) {
		pr_warn("HPET/PMTIMER calibration failed. Using PIT calibration.\n");
		return tsc_pit_min;
	}

	pr_warn("PIT calibration deviates from %s: %lu %lu\n",
		hpet ? "HPET" : "PMTIMER", tsc_pit_min, tsc_ref_min);
	pr_info("Using PIT calibration value\n");
	return tsc_pit_min;
}

unsigned long native_calibrate_cpu_early(void)
{
	unsigned long flags, fast_calibrate = cpu_khz_from_cpuid();

	/* cpu_khz_from_msr removed - was stub returning 0 */
	if (!fast_calibrate) {
		local_irq_save(flags);
		fast_calibrate = quick_pit_calibrate();
		local_irq_restore(flags);
	}
	return fast_calibrate;
}

static unsigned long native_calibrate_cpu(void)
{
	unsigned long tsc_freq = native_calibrate_cpu_early();

	if (!tsc_freq)
		tsc_freq = pit_hpet_ptimer_calibrate_cpu();

	return tsc_freq;
}

/* tsc_save/restore_sched_clock_state removed - never called */

static void tsc_resume(struct clocksource *cs)
{
	/* tsc_verify_tsc_adjust removed - was empty stub */
}

static u64 read_tsc(struct clocksource *cs)
{
	return (u64)rdtsc_ordered();
}

static void tsc_cs_mark_unstable(struct clocksource *cs)
{
	if (tsc_unstable)
		return;

	tsc_unstable = 1;
	/* clear_sched_clock_stable removed - empty stub */
	pr_info("Marking TSC unstable due to clocksource watchdog\n");
}

static void tsc_cs_tick_stable(struct clocksource *cs)
{
	/* sched_clock_tick_stable removed - empty stub */
}

static int tsc_cs_enable(struct clocksource *cs)
{
	vclocks_set_used(VDSO_CLOCKMODE_TSC);
	return 0;
}

static struct clocksource clocksource_tsc_early = {
	.name = "tsc-early",
	.rating = 299,
	.uncertainty_margin = 32 * NSEC_PER_MSEC,
	.read = read_tsc,
	.mask = CLOCKSOURCE_MASK(64),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_MUST_VERIFY,
	.vdso_clock_mode = VDSO_CLOCKMODE_TSC,
	.enable = tsc_cs_enable,
	.resume = tsc_resume,
	.mark_unstable = tsc_cs_mark_unstable,
	.tick_stable = tsc_cs_tick_stable,
	.list = LIST_HEAD_INIT(clocksource_tsc_early.list),
};

static struct clocksource clocksource_tsc = {
	.name = "tsc",
	.rating = 300,
	.read = read_tsc,
	.mask = CLOCKSOURCE_MASK(64),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_VALID_FOR_HRES |
		 CLOCK_SOURCE_MUST_VERIFY | CLOCK_SOURCE_VERIFY_PERCPU,
	.vdso_clock_mode = VDSO_CLOCKMODE_TSC,
	.enable = tsc_cs_enable,
	.resume = tsc_resume,
	.mark_unstable = tsc_cs_mark_unstable,
	.tick_stable = tsc_cs_tick_stable,
	.list = LIST_HEAD_INIT(clocksource_tsc.list),
};

void mark_tsc_unstable(char *reason)
{
	if (tsc_unstable)
		return;

	tsc_unstable = 1;
	/* clear_sched_clock_stable, clocksource_mark_unstable removed - empty stubs */
	pr_info("Marking TSC unstable due to %s\n", reason);
}

/* tsc_disable_clocksource_watchdog, unsynchronized_tsc removed - never called */

static void tsc_refine_calibration_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(tsc_irqwork, tsc_refine_calibration_work);
static void tsc_refine_calibration_work(struct work_struct *work)
{
	/* Stub: TSC refinement not needed for minimal kernel */
	clocksource_register_khz(&clocksource_tsc, tsc_khz);
	clocksource_unregister(&clocksource_tsc_early);
}

static int __init init_tsc_clocksource(void)
{
	if (!boot_cpu_has(X86_FEATURE_TSC) || !tsc_khz)
		return 0;

	if (tsc_unstable)
		goto unreg;

	if (boot_cpu_has(X86_FEATURE_NONSTOP_TSC_S3))
		clocksource_tsc.flags |= CLOCK_SOURCE_SUSPEND_NONSTOP;

	if (boot_cpu_has(X86_FEATURE_TSC_KNOWN_FREQ)) {
		clocksource_register_khz(&clocksource_tsc, tsc_khz);
unreg:
		clocksource_unregister(&clocksource_tsc_early);
		return 0;
	}

	schedule_delayed_work(&tsc_irqwork, 0);
	return 0;
}
device_initcall(init_tsc_clocksource);

static inline void tsc_dbg(const char *s)
{
	while (*s)
		asm volatile("outb %0, $0xe9" : : "a"(*s++));
}

static bool __init determine_cpu_tsc_frequencies(bool early)
{
	tsc_dbg("determine_cpu_tsc_frequencies: start\n");
	WARN_ON(cpu_khz || tsc_khz);

	if (early) {
		tsc_dbg("determine_cpu_tsc_frequencies: early path\n");
		cpu_khz = x86_platform.calibrate_cpu();
		if (tsc_early_khz)
			tsc_khz = tsc_early_khz;
		else
			tsc_khz = x86_platform.calibrate_tsc();
	} else {
		tsc_dbg("determine_cpu_tsc_frequencies: late path\n");
		WARN_ON(x86_platform.calibrate_cpu != native_calibrate_cpu);
		tsc_dbg("determine_cpu_tsc_frequencies: calling pit_hpet_ptimer_calibrate_cpu\n");
		cpu_khz = pit_hpet_ptimer_calibrate_cpu();
		tsc_dbg("determine_cpu_tsc_frequencies: pit_hpet_ptimer returned\n");
	}

	if (tsc_khz == 0)
		tsc_khz = cpu_khz;
	else if (abs(cpu_khz - tsc_khz) * 10 > tsc_khz)
		cpu_khz = tsc_khz;

	if (tsc_khz == 0)
		return false;

	pr_info("Detected %lu.%03lu MHz processor\n",
		(unsigned long)cpu_khz / KHZ, (unsigned long)cpu_khz % KHZ);

	if (cpu_khz != tsc_khz) {
		pr_info("Detected %lu.%03lu MHz TSC",
			(unsigned long)tsc_khz / KHZ,
			(unsigned long)tsc_khz % KHZ);
	}
	return true;
}

static void __init tsc_enable_sched_clock(void)
{
	u64 lpj = (u64)tsc_khz * KHZ;
	do_div(lpj, HZ);
	loops_per_jiffy = lpj;
	use_tsc_delay();

	/* tsc_store_and_check_tsc_adjust removed - was empty stub */
	cyc2ns_init_boot_cpu();
	static_branch_enable(&__use_tsc);
}

void __init tsc_early_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_TSC))
		return;

	/* is_early_uv_system always false - check removed */
	if (!determine_cpu_tsc_frequencies(true))
		return;
	tsc_enable_sched_clock();
}

/* tsc_init removed - never called */
