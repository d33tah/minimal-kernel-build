#ifndef _LINUX_TOPOLOGY_H
#define _LINUX_TOPOLOGY_H

#include <linux/cpumask.h>
#include <linux/mmzone.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <asm/topology.h>

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

#endif  
