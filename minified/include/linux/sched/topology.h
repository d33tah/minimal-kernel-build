 
#ifndef _LINUX_SCHED_TOPOLOGY_H
#define _LINUX_SCHED_TOPOLOGY_H

#include <linux/topology.h>

#include <linux/sched/idle.h>

 

struct sched_domain_attr;

static inline void
partition_sched_domains_locked(int ndoms_new, cpumask_var_t doms_new[],
			       struct sched_domain_attr *dattr_new)
{
}

static inline void
partition_sched_domains(int ndoms_new, cpumask_var_t doms_new[],
			struct sched_domain_attr *dattr_new)
{
}

static inline bool cpus_share_cache(int this_cpu, int that_cpu)
{
	return true;
}


static inline void rebuild_sched_domains_energy(void)
{
}

#ifndef arch_scale_cpu_capacity
 
static __always_inline
unsigned long arch_scale_cpu_capacity(int cpu)
{
	return SCHED_CAPACITY_SCALE;
}
#endif

#ifndef arch_scale_thermal_pressure
static __always_inline
unsigned long arch_scale_thermal_pressure(int cpu)
{
	return 0;
}
#endif

#ifndef arch_update_thermal_pressure
static __always_inline
void arch_update_thermal_pressure(const struct cpumask *cpus,
				  unsigned long capped_frequency)
{ }
#endif

static inline int task_node(const struct task_struct *p)
{
	return cpu_to_node(task_cpu(p));
}

#endif  
