/* Minimal includes for CPU hotplug */
#include <linux/smp.h>
#include <linux/cache.h>


/*
 * CPU hotplug is off (SMP=n) and the only cpuhp_setup_state caller (softirq's
 * NULL SOFTIRQ_DEAD teardown) was a no-op, so the cpuhp_step state table and
 * the __cpuhp_setup_state[_cpuslocked] registration helpers were unreachable
 * and were removed.
 */

#define MASK_DECLARE_1(x)	[x+1][0] = (1UL << (x))
#define MASK_DECLARE_2(x)	MASK_DECLARE_1(x), MASK_DECLARE_1(x+1)
#define MASK_DECLARE_4(x)	MASK_DECLARE_2(x), MASK_DECLARE_2(x+2)
#define MASK_DECLARE_8(x)	MASK_DECLARE_4(x), MASK_DECLARE_4(x+4)

const unsigned long cpu_bit_bitmap[BITS_PER_LONG+1][BITS_TO_LONGS(NR_CPUS)] = {

	MASK_DECLARE_8(0),	MASK_DECLARE_8(8),
	MASK_DECLARE_8(16),	MASK_DECLARE_8(24),
	/*
	 * The `#if BITS_PER_LONG > 32` rows [32..56] are statically dead here:
	 * BITS_PER_LONG is unconditionally 32 (arch/x86 uapi/asm/bitsperlong.h,
	 * "32-bit only kernel"), so the array is [33] rows and the 64-bit-only
	 * MASK_DECLARE_8(32..56) initializers were never compiled. Dropped.
	 */
};

const DECLARE_BITMAP(cpu_all_bits, NR_CPUS) = CPU_BITS_ALL;

struct cpumask __cpu_possible_mask __read_mostly;

struct cpumask __cpu_online_mask __read_mostly;


void __init boot_cpu_init(void)
{
	int cpu = smp_processor_id();


	cpumask_test_and_set_cpu(cpu, &__cpu_online_mask);
	set_cpu_possible(cpu, true);

}

