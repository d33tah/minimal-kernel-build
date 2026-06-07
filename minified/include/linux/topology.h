#ifndef _LINUX_TOPOLOGY_H
#define _LINUX_TOPOLOGY_H

#include <linux/cpumask.h>
#include <linux/bitops.h>
#include <linux/mmzone.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <asm/topology.h>


int arch_update_cpu_topology(void);

#ifndef numa_node_id
static inline int numa_node_id(void)
{
	return cpu_to_node(raw_smp_processor_id());
}
#endif

#ifndef numa_mem_id
static inline int numa_mem_id(void)
{
	return numa_node_id();
}
#endif

/* Removed as unused:
 * - nr_cpus_node, for_each_node_with_cpus
 * - LOCAL_DISTANCE, REMOTE_DISTANCE, DISTANCE_BITS, node_distance, RECLAIM_DISTANCE
 * - PENALTY_FOR_NODE_WITH_CPUS
 * - cpu_to_mem
 * - TOPOLOGY_*_SYSFS defines
 * - topology_*_id macros (physical_package, die, cluster, core, book, drawer, ppin)
 * - topology_*_cpumask macros (sibling, core, cluster, die, book, drawer)
 * - cpu_cpu_mask
 */

#endif  
