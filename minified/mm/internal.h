 
 
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
			__GFP_ATOMIC)

 
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

void __acct_reclaim_writeback(pg_data_t *pgdat, struct folio *folio,
						int nr_throttled);
static inline void acct_reclaim_writeback(struct folio *folio)
{
	pg_data_t *pgdat = folio_pgdat(folio);
	int nr_throttled = atomic_read(&pgdat->nr_writeback_throttled);

	if (nr_throttled)
		__acct_reclaim_writeback(pgdat, folio, nr_throttled);
}

/* wake_throttle_isolated removed - unused */

vm_fault_t do_swap_page(struct vm_fault *vmf);
void folio_rotate_reclaimable(struct folio *folio);
bool __folio_end_writeback(struct folio *folio);
void deactivate_file_folio(struct folio *folio);

void free_pgtables(struct mmu_gather *tlb, struct vm_area_struct *start_vma,
		unsigned long floor, unsigned long ceiling);
void pmd_install(struct mm_struct *mm, pmd_t *pmd, pgtable_t *pte);

struct zap_details;
void unmap_page_range(struct mmu_gather *tlb,
			     struct vm_area_struct *vma,
			     unsigned long addr, unsigned long end,
			     struct zap_details *details);

/* page_cache_ra_order, force_page_cache_ra removed - unused */

unsigned find_lock_entries(struct address_space *mapping, pgoff_t start,
		pgoff_t end, struct folio_batch *fbatch, pgoff_t *indices);
unsigned find_get_entries(struct address_space *mapping, pgoff_t start,
		pgoff_t end, struct folio_batch *fbatch, pgoff_t *indices);
void filemap_free_folio(struct address_space *mapping, struct folio *folio);
int truncate_inode_folio(struct address_space *mapping, struct folio *folio);
bool truncate_inode_partial_folio(struct folio *folio, loff_t start,
		loff_t end);
long invalidate_inode_page(struct page *page);
unsigned long invalidate_mapping_pagevec(struct address_space *mapping,
		pgoff_t start, pgoff_t end, unsigned long *nr_pagevec);

 
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

/* early_memremap_pgprot_adjust removed - inlined */
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
/* user_min_free_kbytes removed - unused */

extern void free_unref_page(struct page *page, unsigned int order);
extern void free_unref_page_list(struct list_head *list);


extern void *memmap_alloc(phys_addr_t size, phys_addr_t align,
			  phys_addr_t min_addr,
			  int nid, bool exact_nid);

/* split_free_page, find_suitable_fallback declaration removed - unused */
/* is_exec_mapping, is_stack_mapping, is_data_mapping removed - unused */

void __vma_link_list(struct mm_struct *mm, struct vm_area_struct *vma,
		struct vm_area_struct *prev);
/* __vma_unlink_list removed - never called */
struct anon_vma *folio_anon_vma(struct folio *folio);

void unmap_mapping_folio(struct folio *folio);
extern long populate_vma_page_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end, int *locked);
/* faultin_vma_page_range, mlock_future_check removed - unused */

/* mlock_folio, munlock_page, mlock_new_page, mlock_page_drain_local,
   mlock_page_drain_remote removed - never called */

/* maybe_pmd_mkwrite removed - unused */


static inline unsigned long
vma_pgoff_address(pgoff_t pgoff, unsigned long nr_pages,
		  struct vm_area_struct *vma)
{
	unsigned long address;

	if (pgoff >= vma->vm_pgoff) {
		address = vma->vm_start +
			((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
		 
		if (address < vma->vm_start || address >= vma->vm_end)
			address = -EFAULT;
	} else if (pgoff + nr_pages - 1 >= vma->vm_pgoff) {
		 
		address = vma->vm_start;
	} else {
		address = -EFAULT;
	}
	return address;
}

 
static inline unsigned long
vma_address(struct page *page, struct vm_area_struct *vma)
{
	/* PageKsm always returns false */
	return vma_pgoff_address(page_to_pgoff(page), compound_nr(page), vma);
}

 
static inline unsigned long vma_address_end(struct page_vma_mapped_walk *pvmw)
{
	struct vm_area_struct *vma = pvmw->vma;
	pgoff_t pgoff;
	unsigned long address;

	 
	if (pvmw->nr_pages == 1)
		return pvmw->address + PAGE_SIZE;

	pgoff = pvmw->pgoff + pvmw->nr_pages;
	address = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
	 
	if (address < vma->vm_start || address > vma->vm_end)
		address = vma->vm_end;
	return address;
}

/* maybe_unlock_mmap_for_io removed - unused */

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

/* hwpoison_filter removed - unused */

extern unsigned long  __must_check vm_mmap_pgoff(struct file *, unsigned long,
        unsigned long, unsigned long,
        unsigned long, unsigned long);

extern void set_pageblock_order(void);
/* reclaim_clean_pages_from_list removed - unused */

#define ALLOC_WMARK_MIN		WMARK_MIN
#define ALLOC_WMARK_LOW		WMARK_LOW
#define ALLOC_WMARK_HIGH	WMARK_HIGH
#define ALLOC_NO_WATERMARKS	0x04  

 
#define ALLOC_WMARK_MASK	(ALLOC_NO_WATERMARKS-1)

 
#define ALLOC_OOM		0x08

#define ALLOC_HARDER		 0x10
#define ALLOC_HIGH		 0x20
#define ALLOC_CPUSET		 0x40
#define ALLOC_KSWAPD		0x800

/* enum ttu_flags, struct tlbflush_unmap_batch forward declarations removed - unused */

/* mm_percpu_wq removed - was allocated but never used */

void flush_tlb_batched_pending(struct mm_struct *mm);

/* pageflag_names, vmaflag_names, gfpflag_names removed - unused (tracing disabled) */
/* is_migrate_highatomic, is_migrate_highatomic_page removed - unused */

void setup_zone_pageset(struct zone *zone);

int vmap_pages_range_noflush(unsigned long addr, unsigned long end,
                pgprot_t prot, struct page **pages, unsigned int page_shift);

void vunmap_range_noflush(unsigned long start, unsigned long end);

DECLARE_PER_CPU(struct per_cpu_nodestat, boot_nodestats);

#endif	 
