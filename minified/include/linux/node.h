/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/node.h - generic node definition
 *
 * This is mainly for topological representation. We define the 
 * basic 'struct node' here, which can be embedded in per-arch 
 * definitions of processors.
 *
 * Basic handling of the devices is done in drivers/base/node.c
 * and system devices are handled in drivers/base/sys.c. 
 *
 * Nodes are exported via driverfs in the class/node/devices/
 * directory. 
 */
#ifndef _LINUX_NODE_H_
#define _LINUX_NODE_H_

#include <linux/device.h>
#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/workqueue.h>

/**
 * struct node_hmem_attrs - heterogeneous memory performance attributes
 *
 * @read_bandwidth:	Read bandwidth in MB/s
 * @write_bandwidth:	Write bandwidth in MB/s
 * @read_latency:	Read latency in nanoseconds
 * @write_latency:	Write latency in nanoseconds
 */
struct node_hmem_attrs {
	unsigned int read_bandwidth;
	unsigned int write_bandwidth;
	unsigned int read_latency;
	unsigned int write_latency;
};

enum cache_indexing {
	NODE_CACHE_DIRECT_MAP,
	NODE_CACHE_INDEXED,
	NODE_CACHE_OTHER,
};

enum cache_write_policy {
	NODE_CACHE_WRITE_BACK,
	NODE_CACHE_WRITE_THROUGH,
	NODE_CACHE_WRITE_OTHER,
};

/**
 * struct node_cache_attrs - system memory caching attributes
 *
 * @indexing:		The ways memory blocks may be placed in cache
 * @write_policy:	Write back or write through policy
 * @size:		Total size of cache in bytes
 * @line_size:		Number of bytes fetched on a cache miss
 * @level:		The cache hierarchy level
 */
struct node_cache_attrs {
	enum cache_indexing indexing;
	enum cache_write_policy write_policy;
	u64 size;
	u16 line_size;
	u8 level;
};

static inline void node_add_cache(unsigned int nid,
				  struct node_cache_attrs *cache_attrs)
{
}

static inline void node_set_perf_attrs(unsigned int nid,
				       struct node_hmem_attrs *hmem_attrs,
				       unsigned access)
{
}

struct node {
	struct device	dev;
	struct list_head access_list;

};

struct memory_block;
extern struct node *node_devices[];
typedef  void (*node_registration_func_t)(struct node *);

static inline void register_memory_blocks_under_node(int nid, unsigned long start_pfn,
						     unsigned long end_pfn,
						     enum meminit_context context)
{
}

extern void unregister_node(struct node *node);
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

static inline void register_hugetlbfs_with_node(node_registration_func_t reg,
						node_registration_func_t unreg)
{
}

#define to_node(device) container_of(device, struct node, dev)

static inline bool node_is_toptier(int node)
{
	return node_state(node, N_CPU);
}

#endif /* _LINUX_NODE_H_ */
