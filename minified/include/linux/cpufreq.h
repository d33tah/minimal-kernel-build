 
 
#ifndef _LINUX_CPUFREQ_H
#define _LINUX_CPUFREQ_H

 
#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/errno.h>

 
struct cpufreq_policy;
struct cpufreq_governor;
struct cpufreq_policy_data;
struct cpufreq_frequency_table;

 
#define CPUFREQ_NAME_LEN		16

 
static inline unsigned int cpufreq_get(unsigned int cpu) { return 0; }
static inline unsigned int cpufreq_quick_get(unsigned int cpu) { return 0; }
static inline unsigned int cpufreq_quick_get_max(unsigned int cpu) { return 0; }
static inline void disable_cpufreq(void) { }
static inline void cpufreq_suspend(void) { }
static inline void cpufreq_resume(void) { }
static inline int cpufreq_boost_trigger_state(int state) { return 0; }
static inline int cpufreq_boost_enabled(void) { return 0; }
static inline int cpufreq_enable_boost_support(void) { return -EINVAL; }

static inline unsigned long cpufreq_scale(unsigned long old, unsigned int div,
		unsigned int mult)
{
#if BITS_PER_LONG == 32
	u64 result = ((u64) old) * ((u64) mult);
	do_div(result, div);
	return (unsigned long) result;
#elif BITS_PER_LONG == 64
	unsigned long result = old * ((u64) mult);
	result /= div;
	return result;
#endif
}

#ifndef arch_set_freq_scale
static __always_inline
void arch_set_freq_scale(const struct cpumask *cpus,
			 unsigned long cur_freq,
			 unsigned long max_freq)
{
}
#endif

 
extern unsigned int arch_freq_get_on_cpu(int cpu);

#endif  
