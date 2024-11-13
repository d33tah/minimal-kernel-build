/* SPDX-License-Identifier: GPL-2.0-or-later */
/* memcontrol.h - Memory Controller
 *
 * Copyright IBM Corporation, 2007
 * Author Balbir Singh <balbir@linux.vnet.ibm.com>
 *
 * Copyright 2007 OpenVZ SWsoft Inc
 * Author: Pavel Emelianov <xemul@openvz.org>
 */

#ifndef _LINUX_MEMCONTROL_H
#define _LINUX_MEMCONTROL_H
#include <linux/cgroup.h>
#include <linux/vm_event_item.h>
#include <linux/hardirq.h>
#include <linux/jump_label.h>
#include <linux/page_counter.h>
#include <linux/vmpressure.h>
#include <linux/eventfd.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/writeback.h>
#include <linux/page-flags.h>

struct mem_cgroup;
struct obj_cgroup;
struct page;
struct mm_struct;
struct kmem_cache;

/* Cgroup-specific page state, on top of universal node page state */
enum memcg_stat_item {
	MEMCG_SWAP = NR_VM_NODE_STAT_ITEMS,
	MEMCG_SOCK,
	MEMCG_PERCPU_B,
	MEMCG_VMALLOC,
	MEMCG_KMEM,
	MEMCG_ZSWAP_B,
	MEMCG_ZSWAPPED,
	MEMCG_NR_STAT,
};

enum memcg_memory_event {
	MEMCG_LOW,
	MEMCG_HIGH,
	MEMCG_MAX,
	MEMCG_OOM,
	MEMCG_OOM_KILL,
	MEMCG_OOM_GROUP_KILL,
	MEMCG_SWAP_HIGH,
	MEMCG_SWAP_MAX,
	MEMCG_SWAP_FAIL,
	MEMCG_NR_MEMORY_EVENTS,
};

struct mem_cgroup_reclaim_cookie {
	pg_data_t *pgdat;
	unsigned int generation;
};


#define MEM_CGROUP_ID_SHIFT	0
#define MEM_CGROUP_ID_MAX	0

static inline struct mem_cgroup *folio_memcg(struct folio *folio)
{
	return NULL;
}

static inline struct mem_cgroup *page_memcg(struct page *page)
{
	return NULL;
}

static inline struct mem_cgroup *folio_memcg_rcu(struct folio *folio)
{
	WARN_ON_ONCE(!rcu_read_lock_held());
	return NULL;
}

static inline struct mem_cgroup *page_memcg_check(struct page *page)
{
	return NULL;
}

static inline bool folio_memcg_kmem(struct folio *folio)
{
	return false;
}

static inline bool PageMemcgKmem(struct page *page)
{
	return false;
}

static inline bool mem_cgroup_is_root(struct mem_cgroup *memcg)
{
	return true;
}

static inline bool mem_cgroup_disabled(void)
{
	return true;
}

static inline void memcg_memory_event(struct mem_cgroup *memcg,
				      enum memcg_memory_event event)
{
}

static inline void memcg_memory_event_mm(struct mm_struct *mm,
					 enum memcg_memory_event event)
{
}

static inline void mem_cgroup_protection(struct mem_cgroup *root,
					 struct mem_cgroup *memcg,
					 unsigned long *min,
					 unsigned long *low)
{
	*min = *low = 0;
}

static inline void mem_cgroup_calculate_protection(struct mem_cgroup *root,
						   struct mem_cgroup *memcg)
{
}

static inline bool mem_cgroup_below_low(struct mem_cgroup *memcg)
{
	return false;
}

static inline bool mem_cgroup_below_min(struct mem_cgroup *memcg)
{
	return false;
}

static inline int mem_cgroup_charge(struct folio *folio,
		struct mm_struct *mm, gfp_t gfp)
{
	return 0;
}

static inline int mem_cgroup_swapin_charge_page(struct page *page,
			struct mm_struct *mm, gfp_t gfp, swp_entry_t entry)
{
	return 0;
}

static inline void mem_cgroup_swapin_uncharge_swap(swp_entry_t entry)
{
}

static inline void mem_cgroup_uncharge(struct folio *folio)
{
}

static inline void mem_cgroup_uncharge_list(struct list_head *page_list)
{
}

static inline void mem_cgroup_migrate(struct folio *old, struct folio *new)
{
}

static inline struct lruvec *mem_cgroup_lruvec(struct mem_cgroup *memcg,
					       struct pglist_data *pgdat)
{
	return &pgdat->__lruvec;
}

static inline struct lruvec *folio_lruvec(struct folio *folio)
{
	struct pglist_data *pgdat = folio_pgdat(folio);
	return &pgdat->__lruvec;
}

static inline
void lruvec_memcg_debug(struct lruvec *lruvec, struct folio *folio)
{
}

static inline struct mem_cgroup *parent_mem_cgroup(struct mem_cgroup *memcg)
{
	return NULL;
}

static inline bool mm_match_cgroup(struct mm_struct *mm,
		struct mem_cgroup *memcg)
{
	return true;
}

static inline struct mem_cgroup *get_mem_cgroup_from_mm(struct mm_struct *mm)
{
	return NULL;
}

static inline
struct mem_cgroup *mem_cgroup_from_css(struct cgroup_subsys_state *css)
{
	return NULL;
}

static inline void obj_cgroup_put(struct obj_cgroup *objcg)
{
}

static inline void mem_cgroup_put(struct mem_cgroup *memcg)
{
}

static inline struct lruvec *folio_lruvec_lock(struct folio *folio)
{
	struct pglist_data *pgdat = folio_pgdat(folio);

	spin_lock(&pgdat->__lruvec.lru_lock);
	return &pgdat->__lruvec;
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

static inline struct mem_cgroup *
mem_cgroup_iter(struct mem_cgroup *root,
		struct mem_cgroup *prev,
		struct mem_cgroup_reclaim_cookie *reclaim)
{
	return NULL;
}

static inline void mem_cgroup_iter_break(struct mem_cgroup *root,
					 struct mem_cgroup *prev)
{
}

static inline int mem_cgroup_scan_tasks(struct mem_cgroup *memcg,
		int (*fn)(struct task_struct *, void *), void *arg)
{
	return 0;
}

static inline unsigned short mem_cgroup_id(struct mem_cgroup *memcg)
{
	return 0;
}

static inline struct mem_cgroup *mem_cgroup_from_id(unsigned short id)
{
	WARN_ON_ONCE(id);
	/* XXX: This should always return root_mem_cgroup */
	return NULL;
}

static inline struct mem_cgroup *mem_cgroup_from_seq(struct seq_file *m)
{
	return NULL;
}

static inline struct mem_cgroup *lruvec_memcg(struct lruvec *lruvec)
{
	return NULL;
}

static inline bool mem_cgroup_online(struct mem_cgroup *memcg)
{
	return true;
}

static inline
unsigned long mem_cgroup_get_zone_lru_size(struct lruvec *lruvec,
		enum lru_list lru, int zone_idx)
{
	return 0;
}

static inline unsigned long mem_cgroup_get_max(struct mem_cgroup *memcg)
{
	return 0;
}

static inline unsigned long mem_cgroup_size(struct mem_cgroup *memcg)
{
	return 0;
}

static inline void
mem_cgroup_print_oom_context(struct mem_cgroup *memcg, struct task_struct *p)
{
}

static inline void
mem_cgroup_print_oom_meminfo(struct mem_cgroup *memcg)
{
}

static inline void lock_page_memcg(struct page *page)
{
}

static inline void unlock_page_memcg(struct page *page)
{
}

static inline void folio_memcg_lock(struct folio *folio)
{
}

static inline void folio_memcg_unlock(struct folio *folio)
{
}

static inline void mem_cgroup_handle_over_high(void)
{
}

static inline void mem_cgroup_enter_user_fault(void)
{
}

static inline void mem_cgroup_exit_user_fault(void)
{
}

static inline bool task_in_memcg_oom(struct task_struct *p)
{
	return false;
}

static inline bool mem_cgroup_oom_synchronize(bool wait)
{
	return false;
}

static inline struct mem_cgroup *mem_cgroup_get_oom_group(
	struct task_struct *victim, struct mem_cgroup *oom_domain)
{
	return NULL;
}

static inline void mem_cgroup_print_oom_group(struct mem_cgroup *memcg)
{
}

static inline void __mod_memcg_state(struct mem_cgroup *memcg,
				     int idx,
				     int nr)
{
}

static inline void mod_memcg_state(struct mem_cgroup *memcg,
				   int idx,
				   int nr)
{
}

static inline void mod_memcg_page_state(struct page *page,
					int idx, int val)
{
}

static inline unsigned long memcg_page_state(struct mem_cgroup *memcg, int idx)
{
	return 0;
}

static inline unsigned long lruvec_page_state(struct lruvec *lruvec,
					      enum node_stat_item idx)
{
	return node_page_state(lruvec_pgdat(lruvec), idx);
}

static inline unsigned long lruvec_page_state_local(struct lruvec *lruvec,
						    enum node_stat_item idx)
{
	return node_page_state(lruvec_pgdat(lruvec), idx);
}

static inline void mem_cgroup_flush_stats(void)
{
}

static inline void mem_cgroup_flush_stats_delayed(void)
{
}

static inline void __mod_memcg_lruvec_state(struct lruvec *lruvec,
					    enum node_stat_item idx, int val)
{
}

static inline void __mod_lruvec_kmem_state(void *p, enum node_stat_item idx,
					   int val)
{
	struct page *page = virt_to_head_page(p);

	__mod_node_page_state(page_pgdat(page), idx, val);
}

static inline void mod_lruvec_kmem_state(void *p, enum node_stat_item idx,
					 int val)
{
	struct page *page = virt_to_head_page(p);

	mod_node_page_state(page_pgdat(page), idx, val);
}

static inline void count_memcg_events(struct mem_cgroup *memcg,
				      enum vm_event_item idx,
				      unsigned long count)
{
}

static inline void __count_memcg_events(struct mem_cgroup *memcg,
					enum vm_event_item idx,
					unsigned long count)
{
}

static inline void count_memcg_page_event(struct page *page,
					  int idx)
{
}

static inline void count_memcg_folio_events(struct folio *folio,
		enum vm_event_item idx, unsigned long nr)
{
}

static inline
void count_memcg_event_mm(struct mm_struct *mm, enum vm_event_item idx)
{
}

static inline void split_page_memcg(struct page *head, unsigned int nr)
{
}

static inline
unsigned long mem_cgroup_soft_limit_reclaim(pg_data_t *pgdat, int order,
					    gfp_t gfp_mask,
					    unsigned long *total_scanned)
{
	return 0;
}

static inline void __inc_lruvec_kmem_state(void *p, enum node_stat_item idx)
{
	__mod_lruvec_kmem_state(p, idx, 1);
}

static inline void __dec_lruvec_kmem_state(void *p, enum node_stat_item idx)
{
	__mod_lruvec_kmem_state(p, idx, -1);
}

static inline struct lruvec *parent_lruvec(struct lruvec *lruvec)
{
	struct mem_cgroup *memcg;

	memcg = lruvec_memcg(lruvec);
	if (!memcg)
		return NULL;
	memcg = parent_mem_cgroup(memcg);
	if (!memcg)
		return NULL;
	return mem_cgroup_lruvec(memcg, lruvec_pgdat(lruvec));
}

static inline void unlock_page_lruvec(struct lruvec *lruvec)
{
	spin_unlock(&lruvec->lru_lock);
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

/* Test requires a stable page->memcg binding, see page_memcg() */
static inline bool folio_matches_lruvec(struct folio *folio,
		struct lruvec *lruvec)
{
	return lruvec_pgdat(lruvec) == folio_pgdat(folio) &&
	       lruvec_memcg(lruvec) == folio_memcg(folio);
}

/* Don't lock again iff page's lruvec locked */
static inline struct lruvec *folio_lruvec_relock_irq(struct folio *folio,
		struct lruvec *locked_lruvec)
{
	if (locked_lruvec) {
		if (folio_matches_lruvec(folio, locked_lruvec))
			return locked_lruvec;

		unlock_page_lruvec_irq(locked_lruvec);
	}

	return folio_lruvec_lock_irq(folio);
}

/* Don't lock again iff page's lruvec locked */
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


static inline struct wb_domain *mem_cgroup_wb_domain(struct bdi_writeback *wb)
{
	return NULL;
}

static inline void mem_cgroup_wb_stats(struct bdi_writeback *wb,
				       unsigned long *pfilepages,
				       unsigned long *pheadroom,
				       unsigned long *pdirty,
				       unsigned long *pwriteback)
{
}

static inline void mem_cgroup_track_foreign_dirty(struct folio *folio,
						  struct bdi_writeback *wb)
{
}

static inline void mem_cgroup_flush_foreign(struct bdi_writeback *wb)
{
}


struct sock;
bool mem_cgroup_charge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages,
			     gfp_t gfp_mask);
void mem_cgroup_uncharge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages);
#define mem_cgroup_sockets_enabled 0
static inline void mem_cgroup_sk_alloc(struct sock *sk) { };
static inline void mem_cgroup_sk_free(struct sock *sk) { };
static inline bool mem_cgroup_under_socket_pressure(struct mem_cgroup *memcg)
{
	return false;
}

static inline void set_shrinker_bit(struct mem_cgroup *memcg,
				    int nid, int shrinker_id)
{
}

static inline bool mem_cgroup_kmem_disabled(void)
{
	return true;
}

static inline int memcg_kmem_charge_page(struct page *page, gfp_t gfp,
					 int order)
{
	return 0;
}

static inline void memcg_kmem_uncharge_page(struct page *page, int order)
{
}

static inline int __memcg_kmem_charge_page(struct page *page, gfp_t gfp,
					   int order)
{
	return 0;
}

static inline void __memcg_kmem_uncharge_page(struct page *page, int order)
{
}

static inline struct obj_cgroup *get_obj_cgroup_from_page(struct page *page)
{
	return NULL;
}

static inline bool memcg_kmem_enabled(void)
{
	return false;
}

static inline int memcg_kmem_id(struct mem_cgroup *memcg)
{
	return -1;
}

static inline struct mem_cgroup *mem_cgroup_from_obj(void *p)
{
       return NULL;
}

static inline void count_objcg_event(struct obj_cgroup *objcg,
				     enum vm_event_item idx)
{
}


static inline bool obj_cgroup_may_zswap(struct obj_cgroup *objcg)
{
	return true;
}
static inline void obj_cgroup_charge_zswap(struct obj_cgroup *objcg,
					   size_t size)
{
}
static inline void obj_cgroup_uncharge_zswap(struct obj_cgroup *objcg,
					     size_t size)
{
}

#endif /* _LINUX_MEMCONTROL_H */
