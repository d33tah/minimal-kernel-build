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

/* cpufreq_get, cpufreq_quick_get*, disable_cpufreq, cpufreq_suspend/resume removed - unused */
/* cpufreq_boost_*, cpufreq_enable_boost_support, cpufreq_scale removed - unused */
/* arch_set_freq_scale removed - unused */

extern unsigned int arch_freq_get_on_cpu(int cpu);

#endif  
