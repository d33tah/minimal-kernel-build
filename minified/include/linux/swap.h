/* Minimal swap.h - stubs for !CONFIG_SWAP */
#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/mmzone.h>
#include <linux/memcontrol.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/atomic.h>
#include <asm/page.h>
#ifdef __KERNEL__
#define SWAP_CLUSTER_MAX 32UL

extern struct list_lru shadow_nodes;
/* dax_mapping and shmem_mapping always return false */
#define mapping_set_update(xas, mapping) do {				\
	xas_set_update(xas, NULL);					\
	xas_set_lru(xas, &shadow_nodes);				\
} while (0)

extern void folio_add_lru(struct folio *);

extern void lru_add_drain(void);
extern void lru_cache_add_inactive_or_unevictable(struct page *page,
						struct vm_area_struct *vma);

#define total_swap_pages			0L

#define free_pages_and_swap_cache(pages, nr) \
	release_pages((pages), (nr));

#endif /* __KERNEL__ */
#endif /* _LINUX_SWAP_H */
