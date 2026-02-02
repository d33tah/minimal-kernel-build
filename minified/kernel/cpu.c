/* Minimal includes for CPU hotplug */
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>

/* cpuhp_cpu_state, cpuhp_step, cpuhp_hp_states array,
   cpuhp_get_step, cpuhp_step_empty, cpuhp_invoke_callback,
   cpuhp_reserve_state, cpuhp_store_callbacks, cpuhp_issue_call,
   cpuhp_rollback_install, __cpuhp_setup_state_cpuslocked,
   __cpuhp_setup_state removed - no callers after CPU hotplug
   callback removal (~180 LOC) */

/* struct cpuhp_cpu_state, cpuhp_state per-CPU removed - write-only */

#define MASK_DECLARE_1(x) [x + 1][0] = (1UL << (x))
#define MASK_DECLARE_2(x) MASK_DECLARE_1(x), MASK_DECLARE_1(x + 1)
#define MASK_DECLARE_4(x) MASK_DECLARE_2(x), MASK_DECLARE_2(x + 2)
#define MASK_DECLARE_8(x) MASK_DECLARE_4(x), MASK_DECLARE_4(x + 4)

const unsigned long cpu_bit_bitmap[BITS_PER_LONG + 1][BITS_TO_LONGS(NR_CPUS)] = {

	MASK_DECLARE_8(0), MASK_DECLARE_8(8), MASK_DECLARE_8(16),
	MASK_DECLARE_8(24),
	/* BITS_PER_LONG == 32, no 64-bit masks */
};

const DECLARE_BITMAP(cpu_all_bits, NR_CPUS) = CPU_BITS_ALL;

struct cpumask __cpu_possible_mask __read_mostly;

struct cpumask __cpu_online_mask __read_mostly;

struct cpumask __cpu_present_mask __read_mostly;

struct cpumask __cpu_active_mask __read_mostly;

/* __num_online_cpus removed - write-only variable, num_online_cpus() not called */

void set_cpu_online(unsigned int cpu, bool online)
{
	if (online)
		cpumask_test_and_set_cpu(cpu, &__cpu_online_mask);
	else
		cpumask_test_and_clear_cpu(cpu, &__cpu_online_mask);
}

void __init boot_cpu_init(void)
{
	int cpu = smp_processor_id();

	set_cpu_online(cpu, true);
	set_cpu_active(cpu, true);
	set_cpu_present(cpu, true);
	set_cpu_possible(cpu, true);
}

void __init boot_cpu_hotplug_init(void)
{
	/* cpuhp_state.state assignment removed - write-only */
}

/* on_each_cpu_cond_mask removed - no callers after TLB simplification */
