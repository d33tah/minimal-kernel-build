/*
 * include/linux/topology.h
 *
 * Written by: Matthew Dobson, IBM Corporation
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <colpatch@us.ibm.com>
 */
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

/* Conform to ACPI 2.0 SLIT distance definitions */
#define LOCAL_DISTANCE		10
#define REMOTE_DISTANCE		20
#define DISTANCE_BITS           8
#ifndef node_distance
#define node_distance(from,to)	((from) == (to) ? LOCAL_DISTANCE : REMOTE_DISTANCE)
#endif
#ifndef RECLAIM_DISTANCE
/*
 * If the distance between nodes in a system is larger than RECLAIM_DISTANCE
 * (in whatever arch specific measurement units returned by node_distance())
 * and node_reclaim_mode is enabled then the VM will only call node_reclaim()
 * on nodes within this distance.
 */
#define RECLAIM_DISTANCE 30
#endif

/*
 * The following tunable allows platforms to override the default node
 * reclaim distance (RECLAIM_DISTANCE) if remote memory accesses are
 * sufficiently fast that the default value actually hurts
 * performance.
 *
 * AMD EPYC machines use this because even though the 2-hop distance
 * is 32 (3.2x slower than a local memory access) performance actually
 * *improves* if allowed to reclaim memory and load balance tasks
 * between NUMA nodes 2-hops apart.
 */
extern int __read_mostly node_reclaim_distance;

#ifndef PENALTY_FOR_NODE_WITH_CPUS
#define PENALTY_FOR_NODE_WITH_CPUS	(1)
#endif


/* Returns the number of the current Node. */
#ifndef numa_node_id
static inline int numa_node_id(void)
{
	return cpu_to_node(raw_smp_processor_id());
}
#endif



#ifndef numa_mem_id
/* Returns the number of the nearest Node with memory */
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


#endif /* _LINUX_TOPOLOGY_H */
