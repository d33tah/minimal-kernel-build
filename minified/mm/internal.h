 
 
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

void __acct_reclaim_writeback(pg_data_t *pgdat, struct folio *folio,
						int nr_throttled);
static inline void acct_reclaim_writeback(struct folio *folio)
{
	pg_data_t *pgdat = folio_pgdat(folio);
	int nr_throttled = atomic_read(&pgdat->nr_writeback_throttled);

	if (nr_throttled)
		__acct_reclaim_writeback(pgdat, folio, nr_throttled);
}

static inline void wake_throttle_isolated(pg_data_t *pgdat)
{
	wait_queue_head_t *wqh;

	wqh = &pgdat->reclaim_wait[VMSCAN_THROTTLE_ISOLATED];
	if (waitqueue_active(wqh))
		wake_up(wqh);
}

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

void page_cache_ra_order(struct readahead_control *, struct file_ra_state *,
		unsigned int order);
void force_page_cache_ra(struct readahead_control *, unsigned long nr);
static inline void force_page_cache_readahead(struct address_space *mapping,
		struct file *file, pgoff_t index, unsigned long nr_to_read)
{
	DEFINE_READAHEAD(ractl, file, &file->f_ra, mapping, index);
	force_page_cache_ra(&ractl, nr_to_read);
}

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

static inline bool page_evictable(struct page *page)
{
	bool ret;

	 
	rcu_read_lock();
	ret = !mapping_unevictable(page_mapping(page)) && !PageMlocked(page);
	rcu_read_unlock();
	return ret;
}

 
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

 
int isolate_lru_page(struct page *page);
int folio_isolate_lru(struct folio *folio);
void putback_lru_page(struct page *page);
void folio_putback_lru(struct folio *folio);
extern void reclaim_throttle(pg_data_t *pgdat, enum vmscan_throttle_state reason);

 
extern pmd_t *mm_find_pmd(struct mm_struct *mm, unsigned long address);

 

 
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

 
static inline bool page_is_buddy(struct page *page, struct page *buddy,
				 unsigned int order)
{
	if (!page_is_guard(buddy) && !PageBuddy(buddy))
		return false;

	if (buddy_order(buddy) != order)
		return false;

	 
	if (page_zone_id(page) != page_zone_id(buddy))
		return false;

	VM_BUG_ON_PAGE(page_count(buddy) != 0, buddy);

	return true;
}

 
static inline unsigned long
__find_buddy_pfn(unsigned long page_pfn, unsigned int order)
{
	return page_pfn ^ (1 << order);
}

 
static inline struct page *find_buddy_page_pfn(struct page *page,
			unsigned long pfn, unsigned int order, unsigned long *buddy_pfn)
{
	unsigned long __buddy_pfn = __find_buddy_pfn(pfn, order);
	struct page *buddy;

	buddy = page + (__buddy_pfn - pfn);
	if (buddy_pfn)
		*buddy_pfn = __buddy_pfn;

	if (page_is_buddy(page, buddy, order))
		return buddy;
	return NULL;
}

extern struct page *__pageblock_pfn_to_page(unsigned long start_pfn,
				unsigned long end_pfn, struct zone *zone);

static inline struct page *pageblock_pfn_to_page(unsigned long start_pfn,
				unsigned long end_pfn, struct zone *zone)
{
	if (zone->contiguous)
		return pfn_to_page(start_pfn);

	return __pageblock_pfn_to_page(start_pfn, end_pfn, zone);
}

extern int __isolate_free_page(struct page *page, unsigned int order);
extern void __putback_isolated_page(struct page *page, unsigned int order,
				    int mt);
extern void memblock_free_pages(struct page *page, unsigned long pfn,
					unsigned int order);
extern void __free_pages_core(struct page *page, unsigned int order);
extern void prep_compound_page(struct page *page, unsigned int order);
extern void post_alloc_hook(struct page *page, unsigned int order,
					gfp_t gfp_flags);
extern int user_min_free_kbytes;

extern void free_unref_page(struct page *page, unsigned int order);
extern void free_unref_page_list(struct list_head *list);

extern void zone_pcp_update(struct zone *zone, int cpu_online);
extern void zone_pcp_reset(struct zone *zone);
extern void zone_pcp_disable(struct zone *zone);
extern void zone_pcp_enable(struct zone *zone);

extern void *memmap_alloc(phys_addr_t size, phys_addr_t align,
			  phys_addr_t min_addr,
			  int nid, bool exact_nid);

int split_free_page(struct page *free_page,
			unsigned int order, unsigned long split_pfn_offset);

int find_suitable_fallback(struct free_area *area, unsigned int order,
			int migratetype, bool only_stealable, bool *can_steal);

 

 
static inline bool is_exec_mapping(vm_flags_t flags)
{
	return (flags & (VM_EXEC | VM_WRITE | VM_STACK)) == VM_EXEC;
}

 
static inline bool is_stack_mapping(vm_flags_t flags)
{
	return (flags & VM_STACK) == VM_STACK;
}

 
static inline bool is_data_mapping(vm_flags_t flags)
{
	return (flags & (VM_WRITE | VM_SHARED | VM_STACK)) == VM_WRITE;
}

 
void __vma_link_list(struct mm_struct *mm, struct vm_area_struct *vma,
		struct vm_area_struct *prev);
void __vma_unlink_list(struct mm_struct *mm, struct vm_area_struct *vma);
struct anon_vma *folio_anon_vma(struct folio *folio);

void unmap_mapping_folio(struct folio *folio);
extern long populate_vma_page_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end, int *locked);
extern long faultin_vma_page_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end,
				   bool write, int *locked);
extern int mlock_future_check(struct mm_struct *mm, unsigned long flags,
			      unsigned long len);
 
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

void munlock_page(struct page *page);
static inline void munlock_vma_page(struct page *page,
			struct vm_area_struct *vma, bool compound)
{
	if (unlikely(vma->vm_flags & VM_LOCKED) &&
	    (compound || !PageTransCompound(page)))
		munlock_page(page);
}
void mlock_new_page(struct page *page);
bool need_mlock_page_drain(int cpu);
void mlock_page_drain_local(void);
void mlock_page_drain_remote(int cpu);

extern pmd_t maybe_pmd_mkwrite(pmd_t pmd, struct vm_area_struct *vma);

 
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
	VM_BUG_ON_PAGE(PageKsm(page), page);	 
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

 
static inline struct page *mem_map_offset(struct page *base, int offset)
{
	if (unlikely(offset >= MAX_ORDER_NR_PAGES))
		return nth_page(base, offset);
	return base + offset;
}

 
static inline struct page *mem_map_next(struct page *iter,
						struct page *base, int offset)
{
	if (unlikely((offset & (MAX_ORDER_NR_PAGES - 1)) == 0)) {
		unsigned long pfn = page_to_pfn(base) + offset;
		if (!pfn_valid(pfn))
			return NULL;
		return pfn_to_page(pfn);
	}
	return iter + 1;
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

static inline void mminit_verify_pageflags_layout(void)
{
}

static inline void mminit_verify_zonelist(void)
{
}

#define NODE_RECLAIM_NOSCAN	-2
#define NODE_RECLAIM_FULL	-1
#define NODE_RECLAIM_SOME	0
#define NODE_RECLAIM_SUCCESS	1

static inline int node_reclaim(struct pglist_data *pgdat, gfp_t mask,
				unsigned int order)
{
	return NODE_RECLAIM_NOSCAN;
}
static inline int find_next_best_node(int node, nodemask_t *used_node_mask)
{
	return NUMA_NO_NODE;
}

 
extern int hwpoison_filter(struct page *p);

extern u32 hwpoison_filter_dev_major;
extern u32 hwpoison_filter_dev_minor;
extern u64 hwpoison_filter_flags_mask;
extern u64 hwpoison_filter_flags_value;
extern u64 hwpoison_filter_memcg;
extern u32 hwpoison_filter_enable;

static inline void clear_hwpoisoned_pages(struct page *memmap, int nr_pages)
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
#define ALLOC_CPUSET		 0x40  
#define ALLOC_CMA		 0x80  
#define ALLOC_NOFRAGMENT	  0x0
#define ALLOC_KSWAPD		0x800  

enum ttu_flags;
struct tlbflush_unmap_batch;


 
extern struct workqueue_struct *mm_percpu_wq;

void try_to_unmap_flush(void);
void try_to_unmap_flush_dirty(void);
void flush_tlb_batched_pending(struct mm_struct *mm);

extern const struct trace_print_flags pageflag_names[];
extern const struct trace_print_flags vmaflag_names[];
extern const struct trace_print_flags gfpflag_names[];

static inline bool is_migrate_highatomic(enum migratetype migratetype)
{
	return migratetype == MIGRATE_HIGHATOMIC;
}

static inline bool is_migrate_highatomic_page(struct page *page)
{
	return get_pageblock_migratetype(page) == MIGRATE_HIGHATOMIC;
}

void setup_zone_pageset(struct zone *zone);

struct migration_target_control {
	int nid;		 
	nodemask_t *nmask;
	gfp_t gfp_mask;
};

 
int vmap_pages_range_noflush(unsigned long addr, unsigned long end,
                pgprot_t prot, struct page **pages, unsigned int page_shift);

void vunmap_range_noflush(unsigned long start, unsigned long end);

int numa_migrate_prep(struct page *page, struct vm_area_struct *vma,
		      unsigned long addr, int page_nid, int *flags);

void free_zone_device_page(struct page *page);

 
struct folio *try_grab_folio(struct page *page, int refs, unsigned int flags);

DECLARE_PER_CPU(struct per_cpu_nodestat, boot_nodestats);

#endif	 
