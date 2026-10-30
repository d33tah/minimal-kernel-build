/* Minimal swap.h - stubs for !CONFIG_SWAP */
#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/spinlock.h>
#include <linux/mmzone.h>
#include <linux/memcontrol.h>
#include <linux/pagemap.h>
#include <asm/page.h>

extern void folio_add_lru(struct folio *);
extern void lru_cache_add(struct page *);
void mark_page_accessed(struct page *);
void folio_mark_accessed(struct folio *);

extern atomic_t lru_disable_count;

static inline bool lru_cache_disabled(void)
{
	return atomic_read(&lru_disable_count);
}

extern void lru_cache_add_inactive_or_unevictable(struct page *page,
						  struct vm_area_struct *vma);

#define total_swap_pages 0L

#define free_pages_and_swap_cache(pages, nr) release_pages((pages), (nr));

#endif /* _LINUX_SWAP_H */
