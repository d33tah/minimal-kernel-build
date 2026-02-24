
#ifndef _LINUX_MEMCONTROL_H
#define _LINUX_MEMCONTROL_H

#include <linux/mm.h>

struct mem_cgroup;
struct page;
struct obj_cgroup;

static inline struct mem_cgroup *folio_memcg(struct folio *folio)
{
	return NULL;
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
	struct page *page = compound_head(virt_to_page(p));
	mod_node_page_state(page_pgdat(page), idx, val);
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

#endif /* _LINUX_MEMCONTROL_H */
