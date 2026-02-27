#ifndef _LINUX_TOPOLOGY_H
#define _LINUX_TOPOLOGY_H

#include <linux/cpumask.h>
#include <linux/mmzone.h>
#include <linux/smp.h>
#include <linux/percpu.h>
/* asm/topology.h inlined */
#include <linux/numa.h>
static inline int numa_node_id(void)
{
	return 0;
}
#define numa_node_id numa_node_id
#ifndef cpu_to_node
#define cpu_to_node(cpu)	((void)(cpu),0)
#endif
#ifndef numa_mem_id
static inline int numa_mem_id(void)
{
	return numa_node_id();
}
#endif

#endif  
