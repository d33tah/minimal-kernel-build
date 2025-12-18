#ifndef _LINUX_NODE_H_
#define _LINUX_NODE_H_

#include <linux/device.h>
#include <linux/cpumask.h>

/* Most of node.h removed - unused NUMA node management infrastructure:
   - struct node, node_hmem_attrs, node_cache_attrs
   - register_one_node, unregister_one_node
   - register_cpu_under_node, unregister_cpu_under_node
   - register_memory_blocks_under_node, unregister_memory_block_under_nodes
   - node_add_cache, node_set_perf_attrs
   - node_is_toptier, to_node macro
*/

static inline void node_dev_init(void) { }

#endif
