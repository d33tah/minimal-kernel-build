#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/timer.h>
/* acpi_pmtmr.h - PMTMR constants removed with pit_hpet_ptimer_calibrate_cpu */
#include <linux/delay.h>
#include <linux/clocksource.h>
#include <linux/percpu.h>
#include <linux/timex.h>
#include <linux/jump_label.h>
#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/static_call_types.h>

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
/* tsc_early_khz removed - never written, check was dead code */

static DEFINE_STATIC_KEY_FALSE(__use_tsc);

/* tsc_clocksource_reliable removed - never used */

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

/* __set_cyc2ns_scale inlined into tsc_early_init */
/* cyc2ns_init_boot_cpu inlined below, cyc2ns_init_secondary_cpus removed */

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

/* no_sched_irq_time, no_tsc_watchdog removed - never used */

/* tsc_read_refs, calc_pmtimer_ref, pit_calibrate_tsc removed with
   pit_hpet_ptimer_calibrate_cpu (~90 LOC) - never called */

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

/* cpu_khz_from_cpuid, pit_hpet_ptimer_calibrate_cpu removed - inlined/never called */

unsigned long native_calibrate_cpu_early(void)
{
	unsigned long flags, fast_calibrate = 0;

	/* Inlined cpu_khz_from_cpuid() */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
	    boot_cpu_data.cpuid_level >= 0x16) {
		unsigned int eax_base_mhz, ebx_max_mhz, ecx_bus_mhz, edx;
		eax_base_mhz = ebx_max_mhz = ecx_bus_mhz = edx = 0;
		cpuid(0x16, &eax_base_mhz, &ebx_max_mhz, &ecx_bus_mhz, &edx);
		fast_calibrate = eax_base_mhz * 1000;
	}

	/* cpu_khz_from_msr removed - was stub returning 0 */
	if (!fast_calibrate) {
		local_irq_save(flags);
		fast_calibrate = quick_pit_calibrate();
		local_irq_restore(flags);
	}
	return fast_calibrate;
}

/* native_calibrate_cpu removed - determine_cpu_tsc_frequencies always called with early=true */

/* tsc_save/restore_sched_clock_state removed - never called */

/* tsc_resume removed - callback never invoked */

static u64 read_tsc(struct clocksource *cs)
{
	return (u64)rdtsc_ordered();
}

/* tsc_cs_mark_unstable, tsc_cs_tick_stable removed - callbacks never invoked */

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
	.list = LIST_HEAD_INIT(clocksource_tsc.list),
};

/* mark_tsc_unstable removed - never called (~8 LOC) */

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

/* tsc_dbg debug function removed */

static bool __init determine_cpu_tsc_frequencies(void)
{
	WARN_ON(cpu_khz || tsc_khz);

	/* Always called with early=true, so else branch removed */
	cpu_khz = x86_platform.calibrate_cpu();
	/* tsc_early_khz check removed - variable was never written */
	tsc_khz = x86_platform.calibrate_tsc();

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

/* tsc_enable_sched_clock, cyc2ns_init_boot_cpu inlined - single callers */
void __init tsc_early_init(void)
{
	u64 lpj;
	struct cyc2ns *c2n;

	if (!boot_cpu_has(X86_FEATURE_TSC))
		return;

	if (!determine_cpu_tsc_frequencies())
		return;

	/* tsc_enable_sched_clock inlined */
	lpj = (u64)tsc_khz * KHZ;
	do_div(lpj, HZ);
	loops_per_jiffy = lpj;
	use_tsc_delay();

	/* cyc2ns_init_boot_cpu, __set_cyc2ns_scale inlined */
	c2n = this_cpu_ptr(&cyc2ns);
	seqcount_latch_init(&c2n->seq);
	{
		unsigned long long tsc_now = rdtsc();
		unsigned long long ns_now = cycles_2_ns(tsc_now);
		struct cyc2ns_data data;

		clocks_calc_mult_shift(&data.cyc2ns_mul, &data.cyc2ns_shift,
				       tsc_khz, NSEC_PER_MSEC, 0);

		if (data.cyc2ns_shift == 32) {
			data.cyc2ns_shift = 31;
			data.cyc2ns_mul >>= 1;
		}

		data.cyc2ns_offset =
			ns_now - mul_u64_u32_shr(tsc_now, data.cyc2ns_mul,
						 data.cyc2ns_shift);

		raw_write_seqcount_latch(&c2n->seq);
		c2n->data[0] = data;
		raw_write_seqcount_latch(&c2n->seq);
		c2n->data[1] = data;
	}
	static_branch_enable(&__use_tsc);
}

/* tsc_init removed - never called */
