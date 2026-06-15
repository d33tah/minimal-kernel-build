/* Minimal memcontrol.h - stub for CONFIG_MEMCG disabled */

#ifndef _LINUX_MEMCONTROL_H
#define _LINUX_MEMCONTROL_H

#include <linux/cgroup.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/writeback.h>

struct mem_cgroup;
struct obj_cgroup;
struct page;
struct mm_struct;
struct kmem_cache;

struct mem_cgroup_reclaim_cookie;

#define MEM_CGROUP_ID_SHIFT	0
#define MEM_CGROUP_ID_MAX	0

static inline struct mem_cgroup *folio_memcg(struct folio *folio)
{
	return NULL;
}


static inline int mem_cgroup_charge(struct folio *folio,
		struct mm_struct *mm, gfp_t gfp)
{
	return 0;
}

static inline void mem_cgroup_uncharge(struct folio *folio)
{
}

static inline struct lruvec *folio_lruvec_lock_irq(struct folio *folio)
{
	struct pglist_data *pgdat = folio_pgdat(folio);
	spin_lock_irq(&pgdat->__lruvec.lru_lock);
	return &pgdat->__lruvec;
}

static inline struct lruvec *folio_lruvec_lock_irqsave(struct folio *folio,
		unsigned long *flagsp)
{
	struct pglist_data *pgdat = folio_pgdat(folio);
	spin_lock_irqsave(&pgdat->__lruvec.lru_lock, *flagsp);
	return &pgdat->__lruvec;
}

static inline struct mem_cgroup *lruvec_memcg(struct lruvec *lruvec)
{
	return NULL;
}

static inline void mod_lruvec_kmem_state(void *p, enum node_stat_item idx,
					 int val)
{
	struct page *page = virt_to_head_page(p);
	mod_node_page_state(page_pgdat(page), idx, val);
}

static inline void unlock_page_lruvec_irq(struct lruvec *lruvec)
{
	spin_unlock_irq(&lruvec->lru_lock);
}

static inline void unlock_page_lruvec_irqrestore(struct lruvec *lruvec,
		unsigned long flags)
{
	spin_unlock_irqrestore(&lruvec->lru_lock, flags);
}

static inline bool folio_matches_lruvec(struct folio *folio,
		struct lruvec *lruvec)
{
	return lruvec_pgdat(lruvec) == folio_pgdat(folio) &&
	       lruvec_memcg(lruvec) == folio_memcg(folio);
}


static inline struct lruvec *folio_lruvec_relock_irqsave(struct folio *folio,
		struct lruvec *locked_lruvec, unsigned long *flags)
{
	if (locked_lruvec) {
		if (folio_matches_lruvec(folio, locked_lruvec))
			return locked_lruvec;
		unlock_page_lruvec_irqrestore(locked_lruvec, *flags);
	}
	return folio_lruvec_lock_irqsave(folio, flags);
}

static inline bool memcg_kmem_enabled(void)
{
	return false;
}

static inline int memcg_kmem_id(struct mem_cgroup *memcg)
{
	return -1;
}

#endif /* _LINUX_MEMCONTROL_H */
