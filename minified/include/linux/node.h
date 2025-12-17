#ifndef _LINUX_NODE_H_
#define _LINUX_NODE_H_

#include <linux/device.h>
#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/workqueue.h>

/* Unused structs/enums node_hmem_attrs, cache_indexing, cache_write_policy removed */
struct node_hmem_attrs;
struct node_cache_attrs;

static inline void node_add_cache(unsigned int nid,
				  struct node_cache_attrs *cache_attrs) { }

static inline void node_set_perf_attrs(unsigned int nid,
				       struct node_hmem_attrs *hmem_attrs,
				       unsigned access) { }

struct node {
	struct device	dev;
	struct list_head access_list;

};

struct memory_block;
/* node_devices, node_registration_func_t removed - unused */

static inline void register_memory_blocks_under_node(int nid, unsigned long start_pfn,
						     unsigned long end_pfn,
						     enum meminit_context context)
{
}

static inline void node_dev_init(void)
{
}
static inline int __register_one_node(int nid)
{
	return 0;
}
static inline int register_one_node(int nid)
{
	return 0;
}
static inline int unregister_one_node(int nid)
{
	return 0;
}
static inline int register_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	return 0;
}
static inline int unregister_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	return 0;
}
static inline void unregister_memory_block_under_nodes(struct memory_block *mem_blk)
{
}

/* register_hugetlbfs_with_node removed - unused */

#define to_node(device) container_of(device, struct node, dev)

static inline bool node_is_toptier(int node)
{
	return node_state(node, N_CPU);
}

#endif  
