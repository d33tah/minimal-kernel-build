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

/* MAX_SWAPFILES_SHIFT, SWP_SWAPIN_ERROR_NUM removed - unused */

#ifdef __KERNEL__

#define SWAP_CLUSTER_MAX 32UL

extern struct list_lru shadow_nodes;
/* workingset_update_node removed - was empty stub */
/* dax_mapping and shmem_mapping always return false */
#define mapping_set_update(xas, mapping) do {				\
	xas_set_update(xas, NULL);					\
	xas_set_lru(xas, &shadow_nodes);				\
} while (0)

/* nr_free_pages removed - never used */

extern void folio_add_lru(struct folio *);
/* lru_cache_add inlined into single caller */
/* mark_page_accessed inlined into single caller */
void folio_mark_accessed(struct folio *);

/* lru_disable_count, lru_cache_disabled removed - never used, always 0/false */

extern void lru_add_drain(void);
extern void lru_add_drain_cpu(int cpu);
extern void lru_cache_add_inactive_or_unevictable(struct page *page,
						struct vm_area_struct *vma);

/* remove_mapping removed - always returned 0, inlined in truncate.c */

#define total_swap_pages			0L

/* free_page_and_swap_cache removed - unused */
#define free_pages_and_swap_cache(pages, nr) \
	release_pages((pages), (nr));

/* cgroup_throttle_swaprate removed - empty stub, no callers */

#endif /* __KERNEL__ */
#endif /* _LINUX_SWAP_H */
