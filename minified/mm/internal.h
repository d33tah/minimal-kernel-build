

#ifndef __MM_INTERNAL_H
#define __MM_INTERNAL_H

#include <linux/rmap.h>

#define GFP_RECLAIM_MASK                                                       \
	(__GFP_RECLAIM | __GFP_HIGH | __GFP_IO | __GFP_FS | __GFP_NOWARN |     \
	 __GFP_RETRY_MAYFAIL | __GFP_NOFAIL | __GFP_NORETRY | __GFP_MEMALLOC | \
	 __GFP_NOMEMALLOC | __GFP_ATOMIC | __GFP_NOLOCKDEP)

#define GFP_BOOT_MASK (__GFP_BITS_MASK & ~(__GFP_RECLAIM | __GFP_IO | __GFP_FS))

#define GFP_CONSTRAINT_MASK (__GFP_HARDWALL | __GFP_THISNODE)

#define GFP_SLAB_BUG_MASK (__GFP_DMA32 | __GFP_HIGHMEM | ~__GFP_BITS_MASK)

#define WARN_ON_ONCE_GFP(cond, gfp)                                      \
	({                                                               \
		static bool __section(".data.once") __warned;            \
		int __ret_warn_once = !!(cond);                          \
		if (unlikely(!(gfp & __GFP_NOWARN) && __ret_warn_once && \
			     !__warned)) {                               \
			__warned = true;                                 \
			WARN_ON(1);                                      \
		}                                                        \
		unlikely(__ret_warn_once);                               \
	})

/* acct_reclaim_writeback removed - reclaim throttling never engaged */

void pmd_install(struct mm_struct *mm, pmd_t *pmd, pgtable_t *pte);

/* force_page_cache_readahead removed - unused */

/* page_evictable removed - unused */

static inline void set_page_refcounted(struct page *page)
{
	VM_BUG_ON_PAGE(PageTail(page), page);
	VM_BUG_ON_PAGE(page_ref_count(page), page);
	set_page_count(page, 1);
}

extern unsigned long highest_memmap_pfn;

/* MAX_RECLAIM_RETRIES removed - never referenced */

/* isolate_lru_page, putback_lru_page removed - never called */

/* page_is_buddy removed - unused */
/* __find_buddy_pfn, find_buddy_page_pfn removed - unused */

/* __pageblock_pfn_to_page, pageblock_pfn_to_page removed - unused */

/* __isolate_free_page, __putback_isolated_page removed - unused */
extern void memblock_free_pages(struct page *page, unsigned long pfn,
				unsigned int order);
extern void __free_pages_core(struct page *page, unsigned int order);

/* split_free_page, find_suitable_fallback declaration removed - unused */
/* is_exec_mapping, is_stack_mapping, is_data_mapping removed - unused */

void __vma_link_list(struct mm_struct *mm, struct vm_area_struct *vma,
		     struct vm_area_struct *prev);

/* populate_vma_page_range / faultin_vma_page_range removed - unused */

/* maybe_pmd_mkwrite removed - unused */

extern unsigned long __must_check vm_mmap_pgoff(struct file *, unsigned long,
						unsigned long, unsigned long,
						unsigned long, unsigned long);

#endif
