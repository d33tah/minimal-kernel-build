/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NUMA_H
#define _LINUX_NUMA_H
#include <linux/types.h>

#define NODES_SHIFT     0

#define MAX_NUMNODES    (1 << NODES_SHIFT)

#define	NUMA_NO_NODE	(-1)

/* optionally keep NUMA memory info available post init */
#define __initdata_or_meminfo __initdata

static inline int numa_map_to_online_node(int node)
{
	return NUMA_NO_NODE;
}
static inline int memory_add_physaddr_to_nid(u64 start)
{
	return 0;
}
static inline int phys_to_target_node(u64 start)
{
	return 0;
}


#endif /* _LINUX_NUMA_H */
