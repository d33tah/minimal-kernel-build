#ifndef _LINUX_TOPOLOGY_H
#define _LINUX_TOPOLOGY_H

#include <linux/mmzone.h>
#include <asm/topology.h>

static inline int numa_mem_id(void)
{
	return numa_node_id();
}

#endif
