
#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <linux/errno.h>
#include <linux/mmdebug.h>
#include <linux/gfp.h>
#include <linux/bug.h>
#include <linux/list.h>
#include <linux/mmzone.h>
#include <linux/rbtree.h>
#include <linux/atomic.h>
#include <linux/debug_locks.h>
#include <linux/mm_types.h>
#include <linux/mmap_lock.h>
#include <linux/range.h>
#include <linux/pfn.h>
#include <linux/bit_spinlock.h>
#include <linux/resource.h>
#include <linux/err.h>
#include <linux/page-flags.h>
#include <linux/tracepoint-defs.h>
#include <linux/overflow.h>

static inline int page_ref_count(const struct page *page)
{
	return atomic_read(&page->_refcount);
}

static inline int folio_ref_count(const struct folio *folio)
{
	return page_ref_count(&folio->page);
}

static inline int page_count(const struct page *page)
{
	return folio_ref_count(page_folio(page));
}

static inline void set_page_count(struct page *page, int v)
{
	atomic_set(&page->_refcount, v);
}


static inline void init_page_count(struct page *page)
{
	set_page_count(page, 1);
}

static inline void page_ref_add(struct page *page, int nr)
{
	atomic_add(nr, &page->_refcount);
}

static inline void folio_ref_add(struct folio *folio, int nr)
{
	page_ref_add(&folio->page, nr);
}

static inline void page_ref_inc(struct page *page)
{
	atomic_inc(&page->_refcount);
}

static inline void folio_ref_inc(struct folio *folio)
{
	page_ref_inc(&folio->page);
}


static inline int page_ref_sub_and_test(struct page *page, int nr)
{
	return atomic_sub_and_test(nr, &page->_refcount);
}

static inline int folio_ref_sub_and_test(struct folio *folio, int nr)
{
	return page_ref_sub_and_test(&folio->page, nr);
}

static inline int page_ref_dec_and_test(struct page *page)
{
	return atomic_dec_and_test(&page->_refcount);
}


static inline bool page_ref_add_unless(struct page *page, int nr, int u)
{
	return atomic_add_unless(&page->_refcount, nr, u);
}

static inline bool folio_ref_add_unless(struct folio *folio, int nr, int u)
{
	return page_ref_add_unless(&folio->page, nr, u);
}


static inline bool folio_ref_try_add_rcu(struct folio *folio, int count)
{
	return folio_ref_add_unless(folio, count, 0);
}

static inline bool folio_try_get_rcu(struct folio *folio)
{
	return folio_ref_try_add_rcu(folio, 1);
}
#include <linux/sizes.h>
#include <linux/sched.h>
#include <linux/pgtable.h>

struct anon_vma;
struct anon_vma_chain;
struct user_struct;
struct pt_regs;


extern unsigned long max_mapnr;

extern atomic_long_t _totalram_pages;
static inline unsigned long totalram_pages(void)
{
	return (unsigned long)atomic_long_read(&_totalram_pages);
}

static inline void totalram_pages_inc(void)
{
	atomic_long_inc(&_totalram_pages);
}

static inline void totalram_pages_add(long count)
{
	atomic_long_add(count, &_totalram_pages);
}

extern void * high_memory;

#define sysctl_legacy_va_layout 0

extern int mmap_rnd_bits __read_mostly;

#include <asm/page.h>
#include <asm/processor.h>

#ifndef untagged_addr
#define untagged_addr(addr) (addr)
#endif

#ifndef page_to_virt
#define page_to_virt(x)	__va(PFN_PHYS(page_to_pfn(x)))
#endif


/* BITS_PER_LONG == 32 */
#define mm_zero_struct_page(pp)  ((void)memset((pp), 0, sizeof(struct page)))

#define MAPCOUNT_ELF_CORE_MARGIN	(5)
#define DEFAULT_MAX_MAP_COUNT	(USHRT_MAX - MAPCOUNT_ELF_CORE_MARGIN)

extern int sysctl_max_map_count;

#define nth_page(page,n) ((page) + (n))

#define PAGE_ALIGN(addr) ALIGN(addr, PAGE_SIZE)

#define PAGE_ALIGNED(addr)	IS_ALIGNED((unsigned long)(addr), PAGE_SIZE)

void setup_initial_init_mm(void *start_code, void *end_code,
			   void *end_data, void *brk);

struct vm_area_struct *vm_area_alloc(struct mm_struct *);
struct vm_area_struct *vm_area_dup(struct vm_area_struct *);
void vm_area_free(struct vm_area_struct *);

#define VM_NONE		0x00000000

#define VM_READ		0x00000001	
#define VM_WRITE	0x00000002
#define VM_EXEC		0x00000004
#define VM_SHARED	0x00000008

#define VM_MAYREAD	0x00000010	
#define VM_MAYWRITE	0x00000020
#define VM_MAYEXEC	0x00000040

#define VM_GROWSDOWN	0x00000100
#define VM_PFNMAP	0x00000400
/* VM_UFFD_WP (0x1000) removed - unused */	

#define VM_LOCKED	0x00002000
#define VM_IO           0x00004000

#define VM_DONTCOPY	0x00020000
#define VM_ACCOUNT	0x00100000

#define VM_MIXEDMAP	0x10000000
#define VM_NOHUGEPAGE	0x40000000


#ifndef VM_GROWSUP
# define VM_GROWSUP	VM_NONE
#endif


#define TASK_EXEC ((current->personality & READ_IMPLIES_EXEC) ? VM_EXEC : 0)

#define VM_DATA_FLAGS_TSK_EXEC	(VM_READ | VM_WRITE | TASK_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)


#ifndef VM_STACK_DEFAULT_FLAGS		
#define VM_STACK_DEFAULT_FLAGS VM_DATA_DEFAULT_FLAGS
#endif

#define VM_STACK	VM_GROWSDOWN

#define VM_STACK_FLAGS	(VM_STACK | VM_STACK_DEFAULT_FLAGS | VM_ACCOUNT)

#define VM_ACCESS_FLAGS (VM_READ | VM_WRITE | VM_EXEC)

#define VM_SPECIAL (VM_IO | VM_PFNMAP | VM_MIXEDMAP)

#define VM_INIT_DEF_MASK	VM_NOHUGEPAGE

#ifndef VM_ARCH_CLEAR
# define VM_ARCH_CLEAR	VM_NONE
#endif

extern pgprot_t protection_map[16];

#define FAULT_FLAG_DEFAULT  (FAULT_FLAG_ALLOW_RETRY)

static inline bool fault_flag_allow_retry_first(enum fault_flag flags)
{
	return (flags & FAULT_FLAG_ALLOW_RETRY) &&
	    (!(flags & FAULT_FLAG_TRIED));
}


struct vm_fault {
	const struct {
		struct vm_area_struct *vma;	
		gfp_t gfp_mask;			
		pgoff_t pgoff;			
		unsigned long address;		
		unsigned long real_address;	
	};
	enum fault_flag flags;		
	pmd_t *pmd;			
	pud_t *pud;			
	union {
		pte_t orig_pte;		
		pmd_t orig_pmd;		
	};

	struct page *cow_page;		
	struct page *page;		
	
	pte_t *pte;			
	spinlock_t *ptl;		
	pgtable_t prealloc_pte;		
};

struct vm_operations_struct {
	vm_fault_t (*fault)(struct vm_fault *vmf);
	vm_fault_t (*map_pages)(struct vm_fault *vmf,
			pgoff_t start_pgoff, pgoff_t end_pgoff);


	vm_fault_t (*page_mkwrite)(struct vm_fault *vmf);
};

static inline void vma_init(struct vm_area_struct *vma, struct mm_struct *mm)
{
	static const struct vm_operations_struct dummy_vm_ops = {};

	memset(vma, 0, sizeof(*vma));
	vma->vm_mm = mm;
	vma->vm_ops = &dummy_vm_ops;
	INIT_LIST_HEAD(&vma->anon_vma_chain);
}

static inline void vma_set_anonymous(struct vm_area_struct *vma)
{
	vma->vm_ops = NULL;
}

static inline bool vma_is_anonymous(struct vm_area_struct *vma)
{
	return !vma->vm_ops;
}

static inline bool vma_is_accessible(struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_ACCESS_FLAGS;
}

struct mmu_gather;
struct inode;

static inline unsigned int compound_order(struct page *page)
{
	if (!PageHead(page))
		return 0;
	return page[1].compound_order;
}

static inline unsigned int folio_order(struct folio *folio)
{
	return compound_order(&folio->page);
}

#include <linux/huge_mm.h>

static inline int put_page_testzero(struct page *page)
{
	VM_BUG_ON_PAGE(page_ref_count(page) == 0, page);
	return page_ref_dec_and_test(page);
}

static inline int folio_put_testzero(struct folio *folio)
{
	return put_page_testzero(&folio->page);
}



#ifndef is_ioremap_addr
#define is_ioremap_addr(x) is_vmalloc_addr(x)
#endif

extern bool is_vmalloc_addr(const void *x);


static inline void page_mapcount_reset(struct page *page)
{
	atomic_set(&(page)->_mapcount, -1);
}

static inline struct page *virt_to_head_page(const void *x)
{
	struct page *page = virt_to_page(x);

	return compound_head(page);
}

static inline struct folio *virt_to_folio(const void *x)
{
	struct page *page = virt_to_page(x);

	return page_folio(page);
}

void __put_page(struct page *page);

void split_page(struct page *page, unsigned int order);

typedef void compound_page_dtor(struct page *);

enum compound_dtor_id {
	NULL_COMPOUND_DTOR,
	COMPOUND_PAGE_DTOR,
	NR_COMPOUND_DTORS,
};
extern compound_page_dtor * const compound_page_dtors[NR_COMPOUND_DTORS];

static inline void set_compound_page_dtor(struct page *page,
		enum compound_dtor_id compound_dtor)
{
	VM_BUG_ON_PAGE(compound_dtor >= NR_COMPOUND_DTORS, page);
	page[1].compound_dtor = compound_dtor;
}

static inline void set_compound_order(struct page *page, unsigned int order)
{
	page[1].compound_order = order;
}

static inline unsigned long compound_nr(struct page *page)
{
	if (!PageHead(page))
		return 1;
	return 1UL << compound_order(page);
}

static inline unsigned long page_size(struct page *page)
{
	return PAGE_SIZE << compound_order(page);
}

static inline int thp_nr_pages(struct page *page)
{
	VM_BUG_ON_PGFLAGS(PageTail(page), page);
	return compound_nr(page);
}

void free_compound_page(struct page *page);

static inline pte_t maybe_mkwrite(pte_t pte, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pte = pte_mkwrite(pte);
	return pte;
}

void do_set_pte(struct vm_fault *vmf, struct page *page, unsigned long addr);

vm_fault_t finish_fault(struct vm_fault *vmf);

#define SECTIONS_PGOFF		((sizeof(unsigned long)*8) - SECTIONS_WIDTH)
#define NODES_PGOFF		(SECTIONS_PGOFF - NODES_WIDTH)
#define ZONES_PGOFF		(NODES_PGOFF - ZONES_WIDTH)

#define NODES_PGSHIFT		(NODES_PGOFF * (NODES_WIDTH != 0))
#define ZONES_PGSHIFT		(ZONES_PGOFF * (ZONES_WIDTH != 0))

#define ZONES_MASK		((1UL << ZONES_WIDTH) - 1)
#define NODES_MASK		((1UL << NODES_WIDTH) - 1)

static inline enum zone_type page_zonenum(const struct page *page)
{
	ASSERT_EXCLUSIVE_BITS(page->flags, ZONES_MASK << ZONES_PGSHIFT);
	return (page->flags >> ZONES_PGSHIFT) & ZONES_MASK;
}

static inline enum zone_type folio_zonenum(const struct folio *folio)
{
	return page_zonenum(&folio->page);
}

#define folio_ref_zero_or_close_to_overflow(folio) \
	((unsigned int) folio_ref_count(folio) + 127u <= 127u)

static inline void folio_get(struct folio *folio)
{
	VM_BUG_ON_FOLIO(folio_ref_zero_or_close_to_overflow(folio), folio);
	folio_ref_inc(folio);
}

static inline void get_page(struct page *page)
{
	folio_get(page_folio(page));
}

static inline void folio_put(struct folio *folio)
{
	if (folio_put_testzero(folio))
		__put_page(&folio->page);
}

static inline void folio_put_refs(struct folio *folio, int refs)
{
	if (folio_ref_sub_and_test(folio, refs))
		__put_page(&folio->page);
}

static inline void put_page(struct page *page)
{
	struct folio *folio = page_folio(page);

	folio_put(folio);
}

static inline bool is_cow_mapping(vm_flags_t flags)
{
	return (flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;
}

#ifdef NODE_NOT_IN_PAGE_FLAGS
extern int page_to_nid(const struct page *page);
#else
static inline int page_to_nid(const struct page *page)
{
	struct page *p = (struct page *)page;

	return (PF_POISONED_CHECK(p)->flags >> NODES_PGSHIFT) & NODES_MASK;
}
#endif

static inline int folio_nid(const struct folio *folio)
{
	return page_to_nid(&folio->page);
}

static inline int page_cpupid_xchg_last(struct page *page, int cpupid)
{
	return page_to_nid(page);
}

static inline struct zone *page_zone(const struct page *page)
{
	return &NODE_DATA(page_to_nid(page))->node_zones[page_zonenum(page)];
}

static inline pg_data_t *page_pgdat(const struct page *page)
{
	return NODE_DATA(page_to_nid(page));
}


static inline pg_data_t *folio_pgdat(const struct folio *folio)
{
	return page_pgdat(&folio->page);
}

static inline void set_page_zone(struct page *page, enum zone_type zone)
{
	page->flags &= ~(ZONES_MASK << ZONES_PGSHIFT);
	page->flags |= (zone & ZONES_MASK) << ZONES_PGSHIFT;
}

static inline void set_page_node(struct page *page, unsigned long node)
{
	page->flags &= ~(NODES_MASK << NODES_PGSHIFT);
	page->flags |= (node & NODES_MASK) << NODES_PGSHIFT;
}

static inline void set_page_links(struct page *page, enum zone_type zone,
	unsigned long node, unsigned long pfn)
{
	set_page_zone(page, zone);
	set_page_node(page, node);
#ifdef SECTION_IN_PAGE_FLAGS
	set_page_section(page, pfn_to_section_nr(pfn));
#endif
}

static inline long folio_nr_pages(struct folio *folio)
{
	return compound_nr(&folio->page);
}


static inline unsigned int folio_shift(struct folio *folio)
{
	return PAGE_SHIFT + folio_order(folio);
}

static inline size_t folio_size(struct folio *folio)
{
	return PAGE_SIZE << folio_order(folio);
}

#include <linux/vmstat.h>

static __always_inline void *lowmem_page_address(const struct page *page)
{
	return page_to_virt(page);
}

#define page_address(page) lowmem_page_address(page)
#define page_address_init()  do { } while(0)

static inline void *folio_address(const struct folio *folio)
{
	return page_address(&folio->page);
}

extern void *page_rmapping(struct page *page);

static inline bool page_is_pfmemalloc(const struct page *page)
{
	
	return (uintptr_t)page->lru.next & BIT(1);
}

static inline void set_page_pfmemalloc(struct page *page)
{
	page->lru.next = (void *)BIT(1);
}

static inline void clear_page_pfmemalloc(struct page *page)
{
	page->lru.next = NULL;
}

#define offset_in_page(p)	((unsigned long)(p) & ~PAGE_MASK)


struct page *vm_normal_page(struct vm_area_struct *vma, unsigned long addr,
			     pte_t pte);



void free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
		unsigned long end, unsigned long floor, unsigned long ceiling);

extern void truncate_pagecache(struct inode *inode, loff_t new);
extern void truncate_setsize(struct inode *inode, loff_t newsize);
void pagecache_isize_extended(struct inode *inode, loff_t from, loff_t to);

extern vm_fault_t handle_mm_fault(struct vm_area_struct *vma,
				  unsigned long address, unsigned int flags,
				  struct pt_regs *regs);
void unmap_mapping_pages(struct address_space *mapping,
		pgoff_t start, pgoff_t nr, bool even_cows);
void unmap_mapping_range(struct address_space *mapping,
		loff_t const holebegin, loff_t const holelen, int even_cows);

long get_user_pages_remote(struct mm_struct *mm,
			    unsigned long start, unsigned long nr_pages,
			    unsigned int gup_flags, struct page **pages,
			    struct vm_area_struct **vmas, int *locked);
/* pin_user_pages_remote, get_user_pages, pin_user_pages, get_user_pages_unlocked,
   pin_user_pages_unlocked, get_user_pages_fast, pin_user_pages_fast removed - unused */



static inline bool folio_mark_dirty(struct folio *folio) { return true; }
bool set_page_dirty(struct page *page);

extern unsigned long move_page_tables(struct vm_area_struct *vma,
		unsigned long old_addr, struct vm_area_struct *new_vma,
		unsigned long new_addr, unsigned long len,
		bool need_rmap_locks);

/*
 * mprotect_fixup is a no-op stub on this minimal build (the only caller,
 * setup_arg_pages() in the exec path, just needs *pprev set and success).
 * Inlined here; mm/mprotect.c was dropped.
 */
static inline int mprotect_fixup(struct mmu_gather *tlb,
			  struct vm_area_struct *vma,
			  struct vm_area_struct **pprev, unsigned long start,
			  unsigned long end, unsigned long newflags)
{
	*pprev = vma;
	return 0;
}


/*
 * RSS accounting (mm_struct.rss_stat) was dropped: nothing in this build ever
 * READS the per-mm counters (no get_mm_rss/get_mm_counter, hiwater/sync stubs).
 * The remaining call sites only ever wrote them, so the writers are no-ops.
 */
static inline void add_mm_counter(struct mm_struct *mm, int member, long value)
{
}

static inline void inc_mm_counter(struct mm_struct *mm, int member)
{
}

/* dec_mm_counter removed - 0 callers (dec_mm_counter_fast macro never invoked) */

static inline int mm_counter_file(struct page *page)
{
	if (PageSwapBacked(page))
		return MM_SHMEMPAGES;
	return MM_FILEPAGES;
}


static inline void update_hiwater_rss(struct mm_struct *mm)
{
}

static inline void sync_mm_rss(struct mm_struct *mm)
{
}

/* Removed: vma_wants_writenotify - never called */

extern pte_t *__get_locked_pte(struct mm_struct *mm, unsigned long addr,
			       spinlock_t **ptl);
static inline pte_t *get_locked_pte(struct mm_struct *mm, unsigned long addr,
				    spinlock_t **ptl)
{
	pte_t *ptep;
	__cond_lock(*ptl, ptep = __get_locked_pte(mm, addr, ptl));
	return ptep;
}

static inline void mm_pgtables_bytes_init(struct mm_struct *mm)
{
}


static inline void mm_inc_nr_ptes(struct mm_struct *mm)
{
}

static inline void mm_dec_nr_ptes(struct mm_struct *mm)
{
}

int __pte_alloc(struct mm_struct *mm, pmd_t *pmd);
int __pte_alloc_kernel(pmd_t *pmd);

static inline p4d_t *p4d_alloc(struct mm_struct *mm, pgd_t *pgd,
		unsigned long address)
{
	return p4d_offset(pgd, address);
}

static inline pud_t *pud_alloc(struct mm_struct *mm, p4d_t *p4d,
		unsigned long address)
{
	return pud_offset(p4d, address);
}

static inline pmd_t *pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address)
{
	return pmd_offset(pud, address);
}

static inline spinlock_t *pte_lockptr(struct mm_struct *mm, pmd_t *pmd)
{
	return &mm->page_table_lock;
}
static inline void pgtable_init(void)
{
	pgtable_cache_init();
}

static inline bool pgtable_pte_page_ctor(struct page *page)
{
	__SetPageTable(page);
	inc_lruvec_page_state(page, NR_PAGETABLE);
	return true;
}

static inline void pgtable_pte_page_dtor(struct page *page)
{
	__ClearPageTable(page);
	dec_lruvec_page_state(page, NR_PAGETABLE);
}

#define pte_offset_map_lock(mm, pmd, address, ptlp)	\
({							\
	spinlock_t *__ptl = pte_lockptr(mm, pmd);	\
	pte_t *__pte = pte_offset_map(pmd, address);	\
	*(ptlp) = __ptl;				\
	spin_lock(__ptl);				\
	__pte;						\
})

#define pte_unmap_unlock(pte, ptl)	do {		\
	spin_unlock(ptl);				\
	pte_unmap(pte);					\
} while (0)

#define pte_alloc(mm, pmd) (unlikely(pmd_none(*(pmd))) && __pte_alloc(mm, pmd))

#define pte_alloc_map_lock(mm, pmd, address, ptlp)	\
	(pte_alloc(mm, pmd) ?			\
		 NULL : pte_offset_map_lock(mm, pmd, address, ptlp))

static inline spinlock_t *pmd_lockptr(struct mm_struct *mm, pmd_t *pmd)
{
	return &mm->page_table_lock;
}

#define pmd_huge_pte(mm, pmd) ((mm)->pmd_huge_pte)

static inline spinlock_t *pmd_lock(struct mm_struct *mm, pmd_t *pmd)
{
	spinlock_t *ptl = pmd_lockptr(mm, pmd);
	spin_lock(ptl);
	return ptl;
}

extern void __init pagecache_init(void);
extern void free_initmem(void);

extern unsigned long free_reserved_area(void *start, void *end,
					int poison, const char *s);

extern void mem_init_print_info(void);

extern void reserve_bootmem_region(phys_addr_t start, phys_addr_t end);

static inline void free_reserved_page(struct page *page)
{
	ClearPageReserved(page);
	init_page_count(page);
	__free_page(page);
	atomic_long_add(1, &page_zone(page)->managed_pages);
	totalram_pages_add(1);
}

static inline unsigned long free_initmem_default(int poison)
{
	extern char __init_begin[], __init_end[];

	return free_reserved_area(&__init_begin, &__init_end,
				  poison, "unused kernel image (initmem)");
}


void free_area_init(unsigned long *max_zone_pfn);
extern int __meminit init_per_zone_wmark_min(void);
extern void mem_init(void);
extern void __init mmap_init(void);

extern void setup_per_cpu_pageset(void);

void vma_interval_tree_insert(struct vm_area_struct *node,
			      struct rb_root_cached *root);
void vma_interval_tree_remove(struct vm_area_struct *node,
			      struct rb_root_cached *root);
struct vm_area_struct *vma_interval_tree_iter_first(struct rb_root_cached *root,
				unsigned long start, unsigned long last);
struct vm_area_struct *vma_interval_tree_iter_next(struct vm_area_struct *node,
				unsigned long start, unsigned long last);

#define vma_interval_tree_foreach(vma, root, start, last)		\
	for (vma = vma_interval_tree_iter_first(root, start, last);	\
	     vma; vma = vma_interval_tree_iter_next(vma, start, last))

void anon_vma_interval_tree_insert(struct anon_vma_chain *node,
				   struct rb_root_cached *root);
void anon_vma_interval_tree_remove(struct anon_vma_chain *node,
				   struct rb_root_cached *root);

extern int __vm_enough_memory(struct mm_struct *mm, long pages, int cap_sys_admin);
extern int __vma_adjust(struct vm_area_struct *vma, unsigned long start,
	unsigned long end, pgoff_t pgoff, struct vm_area_struct *insert,
	struct vm_area_struct *expand);
static inline int vma_adjust(struct vm_area_struct *vma, unsigned long start,
	unsigned long end, pgoff_t pgoff, struct vm_area_struct *insert)
{
	return __vma_adjust(vma, start, end, pgoff, insert, NULL);
}
extern struct anon_vma *find_mergeable_anon_vma(struct vm_area_struct *);
extern int insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void __vma_link_rb(struct mm_struct *, struct vm_area_struct *,
	struct rb_node **, struct rb_node *);
extern void exit_mmap(struct mm_struct *);

extern int set_mm_exe_file(struct mm_struct *mm, struct file *new_exe_file);

unsigned long randomize_stack_top(unsigned long stack_top);
unsigned long randomize_page(unsigned long start, unsigned long range);

extern unsigned long get_unmapped_area(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);

extern unsigned long mmap_region(struct file *file, unsigned long addr,
	unsigned long len, vm_flags_t vm_flags, unsigned long pgoff);
extern unsigned long do_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot, unsigned long flags,
	unsigned long pgoff, unsigned long *populate);
extern int __do_munmap(struct mm_struct *, unsigned long, size_t);

extern int __must_check vm_brk_flags(unsigned long, unsigned long, unsigned long);
extern unsigned long __must_check vm_mmap(struct file *, unsigned long,
        unsigned long, unsigned long,
        unsigned long, unsigned long);

struct vm_unmapped_area_info {
#define VM_UNMAPPED_AREA_TOPDOWN 1
	unsigned long flags;
	unsigned long length;
	unsigned long low_limit;
	unsigned long high_limit;
	unsigned long align_mask;
	unsigned long align_offset;
};

extern unsigned long vm_unmapped_area(struct vm_unmapped_area_info *info);

extern void truncate_inode_pages(struct address_space *, loff_t);

extern vm_fault_t filemap_fault(struct vm_fault *vmf);
extern vm_fault_t filemap_map_pages(struct vm_fault *vmf,
		pgoff_t start_pgoff, pgoff_t end_pgoff);
extern vm_fault_t filemap_page_mkwrite(struct vm_fault *vmf);

extern unsigned long stack_guard_gap;

extern int expand_stack(struct vm_area_struct *vma, unsigned long address);

extern int expand_downwards(struct vm_area_struct *vma,
		unsigned long address);
extern struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr);
extern struct vm_area_struct * find_vma_prev(struct mm_struct * mm, unsigned long addr,
					     struct vm_area_struct **pprev);

static inline
struct vm_area_struct *find_vma_intersection(struct mm_struct *mm,
					     unsigned long start_addr,
					     unsigned long end_addr)
{
	struct vm_area_struct *vma = find_vma(mm, start_addr);

	if (vma && end_addr <= vma->vm_start)
		vma = NULL;
	return vma;
}


static inline unsigned long vm_start_gap(struct vm_area_struct *vma)
{
	unsigned long vm_start = vma->vm_start;

	if (vma->vm_flags & VM_GROWSDOWN) {
		vm_start -= stack_guard_gap;
		if (vm_start > vma->vm_start)
			vm_start = 0;
	}
	return vm_start;
}

static inline unsigned long vm_end_gap(struct vm_area_struct *vma)
{
	/* VM_GROWSUP=VM_NONE (=0) on x86-32: the growsup gap branch is dead */
	return vma->vm_end;
}

static inline unsigned long vma_pages(struct vm_area_struct *vma)
{
	return (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
}

pgprot_t vm_get_page_prot(unsigned long vm_flags);
/* Removed: vma_set_page_prot - never called */


struct vm_area_struct *find_extend_vma(struct mm_struct *, unsigned long addr);


#define FOLL_WRITE	0x01
#define FOLL_TOUCH	0x02
#define FOLL_GET	0x04
#define FOLL_FORCE	0x10
#define FOLL_TRIED	0x800
#define FOLL_REMOTE	0x2000
#define FOLL_COW	0x4000
/* Removed never-set FOLL bits: DUMP/NOWAIT/NOFAULT/HWPOISON/ANON/LONGTERM */

static inline int vm_fault_to_errno(vm_fault_t vm_fault, int foll_flags)
{
	if (vm_fault & VM_FAULT_OOM)
		return -ENOMEM;
	if (vm_fault & (VM_FAULT_HWPOISON | VM_FAULT_HWPOISON_LARGE))
		return -EFAULT; /* FOLL_HWPOISON never set */
	if (vm_fault & (VM_FAULT_SIGBUS | VM_FAULT_SIGSEGV))
		return -EFAULT;
	return 0;
}

static inline bool gup_must_unshare(unsigned int flags, struct page *page)
{
	/* FOLL_PIN never set: (flags & FOLL_WRITE) can never equal FOLL_PIN */
	return false;
}




/* Inlined from elf-randomize.h */
extern unsigned long arch_randomize_brk(struct mm_struct *mm);

#endif 
