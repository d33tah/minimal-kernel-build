#ifndef _LINUX_SCHED_TOPOLOGY_H
#define _LINUX_SCHED_TOPOLOGY_H

#include <linux/topology.h>

#include <linux/sched/idle.h>


/* struct sched_domain_attr, partition_sched_domains removed - unused */

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
