/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MM_SWAP_H
#define _MM_SWAP_H

struct swap_iocb;
static inline int swap_readpage(struct page *page, bool do_poll,
				struct swap_iocb **plug)
{
	return 0;
}
static inline void swap_write_unplug(struct swap_iocb *sio)
{
}

static inline struct address_space *swap_address_space(swp_entry_t entry)
{
	return NULL;
}

static inline void free_swap_cache(struct page *page)
{
}

static inline void show_swap_cache_info(void)
{
}

static inline struct page *swap_cluster_readahead(swp_entry_t entry,
				gfp_t gfp_mask, struct vm_fault *vmf)
{
	return NULL;
}

static inline struct page *swapin_readahead(swp_entry_t swp, gfp_t gfp_mask,
			struct vm_fault *vmf)
{
	return NULL;
}

static inline int swap_writepage(struct page *p, struct writeback_control *wbc)
{
	return 0;
}

static inline struct page *lookup_swap_cache(swp_entry_t swp,
					     struct vm_area_struct *vma,
					     unsigned long addr)
{
	return NULL;
}

static inline
struct page *find_get_incore_page(struct address_space *mapping, pgoff_t index)
{
	return find_get_page(mapping, index);
}

static inline bool add_to_swap(struct folio *folio)
{
	return false;
}

static inline void *get_shadow_from_swap_cache(swp_entry_t entry)
{
	return NULL;
}

static inline int add_to_swap_cache(struct page *page, swp_entry_t entry,
					gfp_t gfp_mask, void **shadowp)
{
	return -1;
}

static inline void __delete_from_swap_cache(struct page *page,
					swp_entry_t entry, void *shadow)
{
}

static inline void delete_from_swap_cache(struct page *page)
{
}

static inline void clear_shadow_from_swap_cache(int type, unsigned long begin,
				unsigned long end)
{
}

static inline unsigned int page_swap_flags(struct page *page)
{
	return 0;
}
#endif /* _MM_SWAP_H */
