#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/timer.h>
#include <linux/clocksource.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/x86_init.h>
#include <asm/apic.h>
#define INTEL_FAM6_ATOM_GOLDMONT 0x5C
#define INTEL_FAM6_ATOM_GOLDMONT_D 0x5F
#include <asm/i8259.h>

unsigned int __read_mostly cpu_khz;

unsigned int __read_mostly tsc_khz;

#define KHZ 1000

static DEFINE_STATIC_KEY_FALSE(__use_tsc);

struct cyc2ns {
	struct cyc2ns_data data[2];
	seqcount_latch_t seq;
};

static DEFINE_PER_CPU_ALIGNED(struct cyc2ns, cyc2ns);

static __always_inline void cyc2ns_read_begin(struct cyc2ns_data *data)
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

static __always_inline void cyc2ns_read_end(void)
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

unsigned long native_calibrate_cpu_early(void)
{
	unsigned long fast_calibrate = 0;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
	    boot_cpu_data.cpuid_level >= 0x16) {
		unsigned int eax_base_mhz, ebx_max_mhz, ecx_bus_mhz, edx;
		eax_base_mhz = ebx_max_mhz = ecx_bus_mhz = edx = 0;
		cpuid(0x16, &eax_base_mhz, &ebx_max_mhz, &ecx_bus_mhz, &edx);
		fast_calibrate = eax_base_mhz * 1000;
	}

	return fast_calibrate;
}

/* Clocksource registration removed - not needed for hello world */

void __init tsc_early_init(void)
{
	u64 lpj;
	struct cyc2ns *c2n;

	if (!boot_cpu_has(X86_FEATURE_TSC))
		return;

	WARN_ON(cpu_khz || tsc_khz);
	cpu_khz = x86_platform.calibrate_cpu();
	tsc_khz = x86_platform.calibrate_tsc();
	if (tsc_khz == 0)
		tsc_khz = cpu_khz;
	else if (abs(cpu_khz - tsc_khz) * 10 > tsc_khz)
		cpu_khz = tsc_khz;
	if (tsc_khz == 0)
		return;
	pr_info("Detected %lu.%03lu MHz processor\n",
		(unsigned long)cpu_khz / KHZ, (unsigned long)cpu_khz % KHZ);
	if (cpu_khz != tsc_khz) {
		pr_info("Detected %lu.%03lu MHz TSC",
			(unsigned long)tsc_khz / KHZ,
			(unsigned long)tsc_khz % KHZ);
	}

	lpj = (u64)tsc_khz * KHZ;
	do_div(lpj, HZ);
	loops_per_jiffy = lpj;
	use_tsc_delay();

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
