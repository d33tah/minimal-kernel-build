/* Minimal includes for CPU hotplug */
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/list.h>


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
#if BITS_PER_LONG > 32
	MASK_DECLARE_8(32),	MASK_DECLARE_8(40),
	MASK_DECLARE_8(48),	MASK_DECLARE_8(56),
#endif
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

