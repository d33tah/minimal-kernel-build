/* Minimal memcontrol.h - stub for CONFIG_MEMCG disabled */

#ifndef _LINUX_MEMCONTROL_H
#define _LINUX_MEMCONTROL_H

#include <linux/cgroup.h>
#include <linux/mm.h>
#include <linux/writeback.h>

struct obj_cgroup;
struct page;

static inline struct lruvec *folio_lruvec_lock_irq(struct folio *folio)
{
	struct pglist_data *pgdat = folio_pgdat(folio);
	spin_lock_irq(&pgdat->__lruvec.lru_lock);
	return &pgdat->__lruvec;
}

static inline void mod_lruvec_kmem_state(void *p, enum node_stat_item idx, int val)
{
	struct page *page = virt_to_head_page(p);
	mod_node_page_state(page_pgdat(page), idx, val);
}

static inline void unlock_page_lruvec_irq(struct lruvec *lruvec)
{
	spin_unlock_irq(&lruvec->lru_lock);
}

#endif /* _LINUX_MEMCONTROL_H */
