/* Minimal swap.h - stubs for !CONFIG_SWAP */
#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/spinlock.h>
#include <linux/linkage.h>
#include <linux/mmzone.h>
#include <linux/list.h>
#include <linux/memcontrol.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/atomic.h>
#include <linux/page-flags.h>
#include <linux/mempolicy.h>
#include <asm/page.h>

struct notifier_block;
struct bio;
struct pagevec;

#define MAX_SWAPFILES_SHIFT	5
#define SWP_SWAPIN_ERROR_NUM 1

#ifdef __KERNEL__

struct address_space;
struct sysinfo;
struct writeback_control;
struct zone;

#define SWAP_CLUSTER_MAX 32UL

void workingset_update_node(struct xa_node *node);
extern struct list_lru shadow_nodes;
/* dax_mapping and shmem_mapping always return false */
#define mapping_set_update(xas, mapping) do {				\
	xas_set_update(xas, workingset_update_node);			\
	xas_set_lru(xas, &shadow_nodes);				\
} while (0)

#define nr_free_pages() global_zone_page_state(NR_FREE_PAGES)

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
extern void lru_cache_add_inactive_or_unevictable(struct page *page,
						struct vm_area_struct *vma);

long remove_mapping(struct address_space *mapping, struct folio *folio);

#define total_swap_pages			0L

#define free_page_and_swap_cache(page) \
	put_page(page)
#define free_pages_and_swap_cache(pages, nr) \
	release_pages((pages), (nr));

static inline void cgroup_throttle_swaprate(struct page *page, gfp_t gfp_mask)
{
}

#endif /* __KERNEL__ */
#endif /* _LINUX_SWAP_H */
