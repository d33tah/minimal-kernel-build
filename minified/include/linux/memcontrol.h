/* Minimal memcontrol.h - stub for CONFIG_MEMCG disabled */

#ifndef _LINUX_MEMCONTROL_H
#define _LINUX_MEMCONTROL_H

#include <linux/cgroup.h>
#include <linux/mm.h>

struct obj_cgroup;
struct page;

static inline void mod_lruvec_kmem_state(void *p, enum node_stat_item idx,
					 int val)
{
	struct page *page = virt_to_head_page(p);
	mod_node_page_state(page_pgdat(page), idx, val);
}

#endif /* _LINUX_MEMCONTROL_H */
