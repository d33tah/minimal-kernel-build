#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/timer.h>
#include <linux/clocksource.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/x86_init.h>
#include <asm/i8259.h>

unsigned int __read_mostly cpu_khz;

unsigned int __read_mostly tsc_khz;

#define KHZ 1000

static DEFINE_STATIC_KEY_FALSE(__use_tsc);

static DEFINE_PER_CPU_ALIGNED(struct cyc2ns_data, cyc2ns);

static __always_inline unsigned long long cycles_2_ns(unsigned long long cyc)
{
	struct cyc2ns_data *data = this_cpu_ptr(&cyc2ns);

	return data->cyc2ns_offset +
	       mul_u64_u32_shr(cyc, data->cyc2ns_mul, data->cyc2ns_shift);
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
	return 0;
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

	if (!boot_cpu_has(X86_FEATURE_TSC))
		return;

	cpu_khz = x86_platform.calibrate_cpu();
	tsc_khz = x86_platform.calibrate_tsc();
	if (tsc_khz == 0)
		tsc_khz = cpu_khz;
	else if ((cpu_khz > tsc_khz ? cpu_khz - tsc_khz : tsc_khz - cpu_khz) *
			 10 >
		 tsc_khz)
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

	{
		struct cyc2ns_data *c2n = this_cpu_ptr(&cyc2ns);
		unsigned long long tsc_now = rdtsc();

		clocks_calc_mult_shift(&c2n->cyc2ns_mul, &c2n->cyc2ns_shift,
				       tsc_khz, NSEC_PER_MSEC, 0);
		if (c2n->cyc2ns_shift == 32) {
			c2n->cyc2ns_shift = 31;
			c2n->cyc2ns_mul >>= 1;
		}
		c2n->cyc2ns_offset = -mul_u64_u32_shr(tsc_now, c2n->cyc2ns_mul,
						      c2n->cyc2ns_shift);
	}
	static_branch_enable(&__use_tsc);
}
