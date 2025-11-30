/* Minimal swap.h - stubs for !CONFIG_SWAP */
#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/spinlock.h>
#include <linux/linkage.h>
#include <linux/mmzone.h>
#include <linux/list.h>
#include <linux/memcontrol.h>
#include <linux/sched.h>
#include <linux/node.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/atomic.h>
#include <linux/page-flags.h>
#include <uapi/linux/mempolicy.h>
#include <asm/page.h>

struct notifier_block;
struct bio;
struct pagevec;

#define MAX_SWAPFILES_SHIFT	5
#define SWP_SWAPIN_ERROR_NUM 1
#define SWP_PTE_MARKER_NUM 0
#define SWP_DEVICE_NUM 0
#define SWP_MIGRATION_NUM 0
#define SWP_HWPOISON_NUM 0
#define MAX_SWAPFILES \
	((1 << MAX_SWAPFILES_SHIFT) - SWP_DEVICE_NUM - \
	SWP_MIGRATION_NUM - SWP_HWPOISON_NUM - \
	SWP_PTE_MARKER_NUM - SWP_SWAPIN_ERROR_NUM)
#define SWP_SWAPIN_ERROR     (MAX_SWAPFILES + SWP_HWPOISON_NUM + \
			     SWP_MIGRATION_NUM + SWP_DEVICE_NUM + \
			     SWP_PTE_MARKER_NUM)

struct reclaim_state {
	unsigned long reclaimed_slab;
};

#ifdef __KERNEL__

struct address_space;
struct sysinfo;
struct writeback_control;
struct zone;
struct swap_info_struct;

#define SWAP_CLUSTER_MAX 32UL
#define COMPACT_CLUSTER_MAX SWAP_CLUSTER_MAX

static inline swp_entry_t folio_swap_entry(struct folio *folio)
{
	swp_entry_t entry = { .val = page_private(&folio->page) };
	return entry;
}

void workingset_age_nonresident(struct lruvec *lruvec, unsigned long nr_pages);
void *workingset_eviction(struct folio *folio, struct mem_cgroup *target_memcg);
void workingset_refault(struct folio *folio, void *shadow);
void workingset_activation(struct folio *folio);

void workingset_update_node(struct xa_node *node);
extern struct list_lru shadow_nodes;
#define mapping_set_update(xas, mapping) do {				\
	if (!dax_mapping(mapping) && !shmem_mapping(mapping)) {		\
		xas_set_update(xas, workingset_update_node);		\
		xas_set_lru(xas, &shadow_nodes);			\
	}								\
} while (0)

extern unsigned long totalreserve_pages;
#define nr_free_pages() global_zone_page_state(NR_FREE_PAGES)

extern void lru_note_cost(struct lruvec *lruvec, bool file,
			  unsigned int nr_pages);
extern void lru_note_cost_folio(struct folio *);
extern void folio_add_lru(struct folio *);
extern void lru_cache_add(struct page *);
void mark_page_accessed(struct page *);
void folio_mark_accessed(struct folio *);

extern atomic_t lru_disable_count;

static inline bool lru_cache_disabled(void)
{
	return atomic_read(&lru_disable_count);
}

extern void lru_add_drain(void);
extern void lru_add_drain_cpu(int cpu);
extern void swap_setup(void);

extern void lru_cache_add_inactive_or_unevictable(struct page *page,
						struct vm_area_struct *vma);

extern unsigned long zone_reclaimable_pages(struct zone *zone);
extern unsigned long try_to_free_pages(struct zonelist *zonelist, int order,
					gfp_t gfp_mask, nodemask_t *mask);
extern unsigned long shrink_all_memory(unsigned long nr_pages);
extern int vm_swappiness;
long remove_mapping(struct address_space *mapping, struct folio *folio);

extern unsigned long reclaim_pages(struct list_head *page_list);
#define node_reclaim_mode 0

extern void check_move_unevictable_pages(struct pagevec *pvec);

extern void kswapd_run(int nid);
extern void kswapd_stop(int nid);

static inline struct swap_info_struct *swp_swap_info(swp_entry_t entry)
{
	return NULL;
}

static inline struct swap_info_struct *get_swap_device(swp_entry_t entry)
{
	return NULL;
}

#define get_nr_swap_pages()			0L
#define total_swap_pages			0L
#define total_swapcache_pages()			0UL
#define vm_swap_full()				0

#define si_swapinfo(val) \
	do { (val)->freeswap = (val)->totalswap = 0; } while (0)

#define free_page_and_swap_cache(page) \
	put_page(page)
#define free_pages_and_swap_cache(pages, nr) \
	release_pages((pages), (nr));

#define free_swap_and_cache(e) is_pfn_swap_entry(e)

static inline void cgroup_throttle_swaprate(struct page *page, gfp_t gfp_mask)
{
}
static inline void folio_throttle_swaprate(struct folio *folio, gfp_t gfp)
{
	cgroup_throttle_swaprate(&folio->page, gfp);
}

#endif /* __KERNEL__ */
#endif /* _LINUX_SWAP_H */
