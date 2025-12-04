#ifndef _LINUX_NUMA_H
#define _LINUX_NUMA_H
#include <linux/types.h>

#define NODES_SHIFT     0

#define MAX_NUMNODES    (1 << NODES_SHIFT)

#define	NUMA_NO_NODE	(-1)

#define __initdata_or_meminfo __initdata

static inline int numa_map_to_online_node(int node)
{
	return NUMA_NO_NODE;
}
/* memory_add_physaddr_to_nid, phys_to_target_node removed - unused */

#endif  
