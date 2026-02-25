
#ifndef _LINUX_MEMCONTROL_H
#define _LINUX_MEMCONTROL_H

#include <linux/mm.h>

struct mem_cgroup;
struct obj_cgroup;

static inline void mod_lruvec_kmem_state(void *p, enum node_stat_item idx,
					 int val)
{
	struct page *page = compound_head(virt_to_page(p));
	mod_node_page_state(page_pgdat(page), idx, val);
}

#endif /* _LINUX_MEMCONTROL_H */
