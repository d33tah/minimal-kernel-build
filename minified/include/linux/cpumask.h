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

extern struct cpumask __cpu_possible_mask;
extern struct cpumask __cpu_online_mask;

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

static __always_inline int cpumask_test_and_set_cpu(int cpu,
						    struct cpumask *cpumask)
{
	return test_and_set_bit(cpumask_check(cpu), cpumask_bits(cpumask));
}

static inline void cpumask_clear(struct cpumask *dstp)
{
	bitmap_zero(cpumask_bits(dstp), nr_cpumask_bits);
}

#define cpumask_of(cpu) (get_cpu_mask(cpu))

static inline unsigned int cpumask_size(void)
{
	return BITS_TO_LONGS(nr_cpumask_bits) * sizeof(long);
}

extern const DECLARE_BITMAP(cpu_all_bits, NR_CPUS);

static inline void set_cpu_possible(unsigned int cpu, bool possible)
{
	if (possible)
		cpumask_set_cpu(cpu, &__cpu_possible_mask);
	else
		cpumask_clear_cpu(cpu, &__cpu_possible_mask);
}

#define to_cpumask(bitmap)                 \
	((struct cpumask *)(1 ? (bitmap) : \
				(void *)sizeof(__check_is_bitmap(bitmap))))

static inline int __check_is_bitmap(const unsigned long *bitmap)
{
	return 1;
}

extern const unsigned long cpu_bit_bitmap[BITS_PER_LONG + 1]
					 [BITS_TO_LONGS(NR_CPUS)];

static inline const struct cpumask *get_cpu_mask(unsigned int cpu)
{
	const unsigned long *p = cpu_bit_bitmap[1 + cpu % BITS_PER_LONG];
	p -= cpu / BITS_PER_LONG;
	return to_cpumask(p);
}

/* NR_CPUS == 1 - simplified */
#define num_possible_cpus() 1U
#define num_present_cpus() 1U

/* NR_CPUS <= BITS_PER_LONG always true */
#define CPU_BITS_ALL \
	{ [BITS_TO_LONGS(NR_CPUS) - 1] = BITMAP_LAST_WORD_MASK(NR_CPUS) }

#endif
