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

/* enum memcg_stat_item, enum memcg_memory_event removed - unused */

static inline struct mem_cgroup *folio_memcg(struct folio *folio)
{
	return NULL;
}


/* PageMemcgKmem, mem_cgroup_charge, mem_cgroup_uncharge,
   mem_cgroup_uncharge_list, folio_lruvec_lock_irq removed - callers already removed */

static inline struct lruvec *folio_lruvec_lock_irqsave(struct folio *folio,
		unsigned long *flagsp)
{
	struct pglist_data *pgdat = folio_pgdat(folio);
	spin_lock_irqsave(&pgdat->__lruvec.lru_lock, *flagsp);
	return &pgdat->__lruvec;
}

/* mem_cgroup_iter, mem_cgroup_get_zone_lru_size removed - callers removed */

static inline struct mem_cgroup *lruvec_memcg(struct lruvec *lruvec)
{
	return NULL;
}

/* lock_page_memcg, unlock_page_memcg, mem_cgroup_handle_over_high,
   mem_cgroup_enter_user_fault, mem_cgroup_exit_user_fault,
   mem_cgroup_oom_synchronize, mod_memcg_page_state removed - callers removed */


static inline void mod_lruvec_kmem_state(void *p, enum node_stat_item idx,
					 int val)
{
	struct page *page = virt_to_head_page(p);
	mod_node_page_state(page_pgdat(page), idx, val);
}

/* unlock_page_lruvec_irq removed - only irqrestore variant is used */

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

/* set_shrinker_bit, __memcg_kmem_charge_page, __memcg_kmem_uncharge_page,
   memcg_kmem_enabled, task_in_memcg_oom removed - callers already removed */

static inline int memcg_kmem_id(struct mem_cgroup *memcg)
{
	return -1;
}

#endif /* _LINUX_MEMCONTROL_H */
