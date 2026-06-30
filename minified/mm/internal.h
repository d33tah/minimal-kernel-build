 
 
#ifndef __MM_INTERNAL_H
#define __MM_INTERNAL_H

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/tracepoint-defs.h>

struct folio_batch;

 
#define GFP_RECLAIM_MASK (__GFP_RECLAIM|__GFP_HIGH|__GFP_IO|__GFP_FS|\
			__GFP_NOWARN|__GFP_RETRY_MAYFAIL|__GFP_NOFAIL|\
			__GFP_NORETRY|__GFP_MEMALLOC|__GFP_NOMEMALLOC|\
			__GFP_ATOMIC|__GFP_NOLOCKDEP)

 
#define GFP_BOOT_MASK (__GFP_BITS_MASK & ~(__GFP_RECLAIM|__GFP_IO|__GFP_FS))

 
#define GFP_CONSTRAINT_MASK (__GFP_HARDWALL|__GFP_THISNODE)

 
#define GFP_SLAB_BUG_MASK (__GFP_DMA32|__GFP_HIGHMEM|~__GFP_BITS_MASK)

 
#define WARN_ON_ONCE_GFP(cond, gfp)	({				\
	static bool __section(".data.once") __warned;			\
	int __ret_warn_once = !!(cond);					\
									\
	if (unlikely(!(gfp & __GFP_NOWARN) && __ret_warn_once && !__warned)) { \
		__warned = true;					\
		WARN_ON(1);						\
	}								\
	unlikely(__ret_warn_once);					\
})

static inline void *folio_raw_mapping(struct folio *folio)
{
	unsigned long mapping = (unsigned long)folio->mapping;

	return (void *)(mapping & ~PAGE_MAPPING_FLAGS);
}

/* acct_reclaim_writeback removed - reclaim throttling never engaged */

void pmd_install(struct mm_struct *mm, pmd_t *pmd, pgtable_t *pte);


/* force_page_cache_readahead removed - unused */

int truncate_inode_folio(struct address_space *mapping, struct folio *folio);
bool truncate_inode_partial_folio(struct folio *folio, loff_t start,
		loff_t end);

 
static inline bool folio_evictable(struct folio *folio)
{
	bool ret;

	 
	rcu_read_lock();
	ret = !mapping_unevictable(folio_mapping(folio));
	rcu_read_unlock();
	return ret;
}

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

 

 
struct alloc_context {
	struct zonelist *zonelist;
	struct zoneref *preferred_zoneref;
	int migratetype;

	 
	enum zone_type highest_zoneidx;
};

 
static inline unsigned int buddy_order(struct page *page)
{
	 
	return page_private(page);
}

 
#define buddy_order_unsafe(page)	READ_ONCE(page_private(page))

/* page_is_buddy removed - unused */
/* __find_buddy_pfn, find_buddy_page_pfn removed - unused */

/* __pageblock_pfn_to_page, pageblock_pfn_to_page removed - unused */

/* __isolate_free_page, __putback_isolated_page removed - unused */
extern void memblock_free_pages(struct page *page, unsigned long pfn,
					unsigned int order);
extern void __free_pages_core(struct page *page, unsigned int order);

extern void free_unref_page(struct page *page, unsigned int order);

/* split_free_page, find_suitable_fallback declaration removed - unused */
/* is_exec_mapping, is_stack_mapping, is_data_mapping removed - unused */

void __vma_link_list(struct mm_struct *mm, struct vm_area_struct *vma,
		struct vm_area_struct *prev);

/* populate_vma_page_range / faultin_vma_page_range removed - unused */

/* maybe_pmd_mkwrite removed - unused */


static inline struct file *maybe_unlock_mmap_for_io(struct vm_fault *vmf,
						    struct file *fpin)
{
	int flags = vmf->flags;

	if (fpin)
		return fpin;

	 
	if (fault_flag_allow_retry_first(flags)) {
		fpin = get_file(vmf->vma->vm_file);
		mmap_read_unlock(vmf->vma->vm_mm);
	}
	return fpin;
}

extern unsigned long  __must_check vm_mmap_pgoff(struct file *, unsigned long,
        unsigned long, unsigned long,
        unsigned long, unsigned long);


/* ALLOC_WMARK_MIN, ALLOC_WMARK_HIGH removed - never referenced */
#define ALLOC_WMARK_LOW		WMARK_LOW
#define ALLOC_NO_WATERMARKS	0x04  

 
#define ALLOC_WMARK_MASK	(ALLOC_NO_WATERMARKS-1)
/* ALLOC_OOM/HARDER/HIGH/KSWAPD removed - never set on this build */



void flush_tlb_batched_pending(struct mm_struct *mm);

extern const struct trace_print_flags pageflag_names[];
extern const struct trace_print_flags vmaflag_names[];
extern const struct trace_print_flags gfpflag_names[];

/* is_migrate_highatomic, is_migrate_highatomic_page removed - unused */

void setup_zone_pageset(struct zone *zone);

struct migration_target_control {
	int nid;		 
	nodemask_t *nmask;
	gfp_t gfp_mask;
};

#endif
