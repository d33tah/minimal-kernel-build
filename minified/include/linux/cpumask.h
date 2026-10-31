#ifndef __LINUX_CPUMASK_H
#define __LINUX_CPUMASK_H

#include <linux/threads.h>
#include <linux/bitmap.h>

typedef struct cpumask {
	DECLARE_BITMAP(bits, NR_CPUS);
} cpumask_t;

#define cpumask_bits(maskp) ((maskp)->bits)

/*
 * NR_CPUS is fixed to 1 in this build (CONFIG_NR_CPUS=1, unconditional in
 * autoconf.h + threads.h), so `#if NR_CPUS == 1` is always true; the
 * multi-CPU `#else extern unsigned int nr_cpu_ids;` arm was statically dead
 * (no out-of-line definition of nr_cpu_ids exists anyway). Emit the fixed
 * one-CPU value unconditionally.
 */
#define nr_cpu_ids 1U

#define nr_cpumask_bits ((unsigned int)NR_CPUS)

static __always_inline unsigned int cpumask_check(unsigned int cpu)
{
	return cpu;
}

#define CPU_BITS_NONE { [0 ... BITS_TO_LONGS(NR_CPUS) - 1] = 0UL }

static __always_inline void cpumask_set_cpu(unsigned int cpu,
					    struct cpumask *dstp)
{
	set_bit(cpumask_check(cpu), cpumask_bits(dstp));
}

static __always_inline void cpumask_clear_cpu(int cpu, struct cpumask *dstp)
{
	clear_bit(cpumask_check(cpu), cpumask_bits(dstp));
}

static __always_inline int cpumask_test_cpu(int cpu,
					    const struct cpumask *cpumask)
{
	return test_bit(cpumask_check(cpu), cpumask_bits((cpumask)));
}

static inline void cpumask_clear(struct cpumask *dstp)
{
	bitmap_zero(cpumask_bits(dstp), nr_cpumask_bits);
}

static inline unsigned int cpumask_size(void)
{
	return BITS_TO_LONGS(nr_cpumask_bits) * sizeof(long);
}

/* NR_CPUS == 1 - simplified */
#define num_possible_cpus() 1U
#define num_present_cpus() 1U

#endif
