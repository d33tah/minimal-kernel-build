 
 
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

void page_writeback_init(void);

static inline void *folio_raw_mapping(struct folio *folio)
{
	unsigned long mapping = (unsigned long)folio->mapping;

	return (void *)(mapping & ~PAGE_MAPPING_FLAGS);
}

/* acct_reclaim_writeback removed - reclaim throttling never engaged */

vm_fault_t do_swap_page(struct vm_fault *vmf);

void free_pgtables(struct mmu_gather *tlb, struct vm_area_struct *start_vma,
		unsigned long floor, unsigned long ceiling);
void pmd_install(struct mm_struct *mm, pmd_t *pmd, pgtable_t *pte);

struct zap_details;
void unmap_page_range(struct mmu_gather *tlb,
			     struct vm_area_struct *vma,
			     unsigned long addr, unsigned long end,
			     struct zap_details *details);

void page_cache_ra_order(struct readahead_control *, struct file_ra_state *,
		unsigned int order);
/* force_page_cache_readahead removed - unused */

unsigned find_lock_entries(struct address_space *mapping, pgoff_t start,
		pgoff_t end, struct folio_batch *fbatch, pgoff_t *indices);
unsigned find_get_entries(struct address_space *mapping, pgoff_t start,
		pgoff_t end, struct folio_batch *fbatch, pgoff_t *indices);
void filemap_free_folio(struct address_space *mapping, struct folio *folio);
int truncate_inode_folio(struct address_space *mapping, struct folio *folio);
bool truncate_inode_partial_folio(struct folio *folio, loff_t start,
		loff_t end);

 
static inline bool folio_evictable(struct folio *folio)
{
	bool ret;

	 
	rcu_read_lock();
	ret = !mapping_unevictable(folio_mapping(folio)) &&
			!folio_test_mlocked(folio);
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

 
#define MAX_RECLAIM_RETRIES 16

 
pgprot_t __init early_memremap_pgprot_adjust(resource_size_t phys_addr,
					unsigned long size, pgprot_t prot);


/* isolate_lru_page, putback_lru_page removed - never called */

 

 
struct alloc_context {
	struct zonelist *zonelist;
	nodemask_t *nodemask;
	struct zoneref *preferred_zoneref;
	int migratetype;

	 
	enum zone_type highest_zoneidx;
	bool spread_dirty_pages;
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
extern void prep_compound_page(struct page *page, unsigned int order);
extern void post_alloc_hook(struct page *page, unsigned int order,
					gfp_t gfp_flags);
extern int user_min_free_kbytes;

extern void free_unref_page(struct page *page, unsigned int order);
extern void free_unref_page_list(struct list_head *list);


extern void *memmap_alloc(phys_addr_t size, phys_addr_t align,
			  phys_addr_t min_addr,
			  int nid, bool exact_nid);

/* split_free_page, find_suitable_fallback declaration removed - unused */
/* is_exec_mapping, is_stack_mapping, is_data_mapping removed - unused */

void __vma_link_list(struct mm_struct *mm, struct vm_area_struct *vma,
		struct vm_area_struct *prev);

void unmap_mapping_folio(struct folio *folio);
/* populate_vma_page_range / faultin_vma_page_range removed - unused */

void mlock_folio(struct folio *folio);
static inline void mlock_vma_folio(struct folio *folio,
			struct vm_area_struct *vma, bool compound)
{
	 
	if (unlikely((vma->vm_flags & (VM_LOCKED|VM_SPECIAL)) == VM_LOCKED) &&
	    (compound || !folio_test_large(folio)))
		mlock_folio(folio);
}

static inline void mlock_vma_page(struct page *page,
			struct vm_area_struct *vma, bool compound)
{
	mlock_vma_folio(page_folio(page), vma, compound);
}

void mlock_new_page(struct page *page);
void mlock_page_drain_local(void);

/* maybe_pmd_mkwrite removed - unused */


static inline struct file *maybe_unlock_mmap_for_io(struct vm_fault *vmf,
						    struct file *fpin)
{
	int flags = vmf->flags;

	if (fpin)
		return fpin;

	 
	if (fault_flag_allow_retry_first(flags) &&
	    !(flags & FAULT_FLAG_RETRY_NOWAIT)) {
		fpin = get_file(vmf->vma->vm_file);
		mmap_read_unlock(vmf->vma->vm_mm);
	}
	return fpin;
}

enum mminit_level {
	MMINIT_WARNING,
	MMINIT_VERIFY,
	MMINIT_TRACE
};


static inline void mminit_dprintk(enum mminit_level level,
				const char *prefix, const char *fmt, ...)
{
}

static inline void mminit_verify_zonelist(void)
{
}

extern unsigned long  __must_check vm_mmap_pgoff(struct file *, unsigned long,
        unsigned long, unsigned long,
        unsigned long, unsigned long);

extern void set_pageblock_order(void);
unsigned int reclaim_clean_pages_from_list(struct zone *zone,
					    struct list_head *page_list);
 
#define ALLOC_WMARK_MIN		WMARK_MIN
#define ALLOC_WMARK_LOW		WMARK_LOW
#define ALLOC_WMARK_HIGH	WMARK_HIGH
#define ALLOC_NO_WATERMARKS	0x04  

 
#define ALLOC_WMARK_MASK	(ALLOC_NO_WATERMARKS-1)

 
#define ALLOC_OOM		0x08

#define ALLOC_HARDER		 0x10  
#define ALLOC_HIGH		 0x20
#define ALLOC_KSWAPD		0x800

enum ttu_flags;
struct tlbflush_unmap_batch;


 
extern struct workqueue_struct *mm_percpu_wq;

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

 
int vmap_pages_range_noflush(unsigned long addr, unsigned long end,
                pgprot_t prot, struct page **pages, unsigned int page_shift);

void vunmap_range_noflush(unsigned long start, unsigned long end);

DECLARE_PER_CPU(struct per_cpu_nodestat, boot_nodestats);

#endif	 
