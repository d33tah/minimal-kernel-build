#ifndef _LINUX_SCHED_TOPOLOGY_H
#define _LINUX_SCHED_TOPOLOGY_H

#include <linux/topology.h>

#include <linux/sched/idle.h>


struct sched_domain_attr;

/* partition_sched_domains_locked removed - unused */

static inline void
partition_sched_domains(int ndoms_new, cpumask_var_t doms_new[],
			struct sched_domain_attr *dattr_new)
{
}

/* cpus_share_cache removed - unused */

/* rebuild_sched_domains_energy removed - unused */

/* arch_scale_cpu_capacity removed - unused */

#ifndef arch_scale_thermal_pressure
static __always_inline
unsigned long arch_scale_thermal_pressure(int cpu)
{
	return 0;
}
#endif

/* arch_update_thermal_pressure removed - unused */

/* task_node removed - unused */

#endif  
