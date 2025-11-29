 
 
#ifndef _LINUX_NODE_H_
#define _LINUX_NODE_H_

#include <linux/device.h>
#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/workqueue.h>

 
struct node_hmem_attrs {
	unsigned int read_bandwidth;
	unsigned int write_bandwidth;
	unsigned int read_latency;
	unsigned int write_latency;
};

/* cache_indexing, cache_write_policy, node_cache_attrs - not used in minimal kernel */
enum cache_indexing { NODE_CACHE_OTHER };
enum cache_write_policy { NODE_CACHE_WRITE_OTHER };
struct node_cache_attrs;

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

#endif  
