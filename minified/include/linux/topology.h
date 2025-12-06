#ifndef _LINUX_TOPOLOGY_H
#define _LINUX_TOPOLOGY_H


#include <linux/cpumask.h>
#include <linux/bitops.h>
#include <linux/mmzone.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <asm/topology.h>

#ifndef nr_cpus_node
#define nr_cpus_node(node) cpumask_weight(cpumask_of_node(node))
#endif

#define for_each_node_with_cpus(node)			\
	for_each_online_node(node)			\
		if (nr_cpus_node(node))

int arch_update_cpu_topology(void);

#define LOCAL_DISTANCE		10
#define REMOTE_DISTANCE		20
#define DISTANCE_BITS           8
#ifndef node_distance
#define node_distance(from,to)	((from) == (to) ? LOCAL_DISTANCE : REMOTE_DISTANCE)
#endif
#ifndef RECLAIM_DISTANCE
#define RECLAIM_DISTANCE 30
#endif

extern int __read_mostly node_reclaim_distance;

#ifndef PENALTY_FOR_NODE_WITH_CPUS
#define PENALTY_FOR_NODE_WITH_CPUS	(1)
#endif


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

#ifndef cpu_to_mem
static inline int cpu_to_mem(int cpu)
{
	return cpu_to_node(cpu);
}
#endif


#if defined(topology_die_id) && defined(topology_die_cpumask)
#define TOPOLOGY_DIE_SYSFS
#endif
#if defined(topology_cluster_id) && defined(topology_cluster_cpumask)
#define TOPOLOGY_CLUSTER_SYSFS
#endif
#if defined(topology_book_id) && defined(topology_book_cpumask)
#define TOPOLOGY_BOOK_SYSFS
#endif
#if defined(topology_drawer_id) && defined(topology_drawer_cpumask)
#define TOPOLOGY_DRAWER_SYSFS
#endif

#ifndef topology_physical_package_id
#define topology_physical_package_id(cpu)	((void)(cpu), -1)
#endif
#ifndef topology_die_id
#define topology_die_id(cpu)			((void)(cpu), -1)
#endif
#ifndef topology_cluster_id
#define topology_cluster_id(cpu)		((void)(cpu), -1)
#endif
#ifndef topology_core_id
#define topology_core_id(cpu)			((void)(cpu), 0)
#endif
#ifndef topology_book_id
#define topology_book_id(cpu)			((void)(cpu), -1)
#endif
#ifndef topology_drawer_id
#define topology_drawer_id(cpu)			((void)(cpu), -1)
#endif
#ifndef topology_ppin
#define topology_ppin(cpu)			((void)(cpu), 0ull)
#endif
#ifndef topology_sibling_cpumask
#define topology_sibling_cpumask(cpu)		cpumask_of(cpu)
#endif
#ifndef topology_core_cpumask
#define topology_core_cpumask(cpu)		cpumask_of(cpu)
#endif
#ifndef topology_cluster_cpumask
#define topology_cluster_cpumask(cpu)		cpumask_of(cpu)
#endif
#ifndef topology_die_cpumask
#define topology_die_cpumask(cpu)		cpumask_of(cpu)
#endif
#ifndef topology_book_cpumask
#define topology_book_cpumask(cpu)		cpumask_of(cpu)
#endif
#ifndef topology_drawer_cpumask
#define topology_drawer_cpumask(cpu)		cpumask_of(cpu)
#endif


static inline const struct cpumask *cpu_cpu_mask(int cpu)
{
	return cpumask_of_node(cpu_to_node(cpu));
}


#endif  
