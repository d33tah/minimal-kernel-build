
#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <linux/mmdebug.h>
#include <linux/gfp.h>
#include <linux/bug.h>
#include <linux/list.h>
#include <linux/mmzone.h>
#include <linux/rbtree.h>
#include <linux/atomic.h>
#include <linux/mm_types.h>
/* mmap_lock.h inlined - single includer */
/* lockdep.h, rwsem.h, types.h included elsewhere; mm_types.h, mmdebug.h already above */

#define MMAP_LOCK_INITIALIZER(name) \
	.mmap_lock = __RWSEM_INITIALIZER((name).mmap_lock),

static inline int mmap_write_lock_killable(struct mm_struct *mm)
{
	return down_write_killable(&mm->mmap_lock);
}

static inline void mmap_write_unlock(struct mm_struct *mm)
{
	up_write(&mm->mmap_lock);
}

static inline void mmap_read_lock(struct mm_struct *mm)
{
	down_read(&mm->mmap_lock);
}

static inline int mmap_read_lock_killable(struct mm_struct *mm)
{
	return down_read_killable(&mm->mmap_lock);
}

static inline void mmap_read_unlock(struct mm_struct *mm)
{
	up_read(&mm->mmap_lock);
}

/* mmap_read_unlock_non_owner, mmap_assert_write_locked,
 * mmap_lock_is_contended removed - unused */

static inline void mmap_assert_locked(struct mm_struct *mm)
{
	VM_BUG_ON_MM(!rwsem_is_locked(&mm->mmap_lock), mm);
}
#include <linux/pfn.h>
/* percpu-refcount.h, bit_spinlock.h, shrinker.h, resource.h,
   err.h, tracepoint-defs.h, overflow.h, page_ext.h removed - unused */
#include <linux/page-flags.h>

static inline int page_ref_count(const struct page *page)
{
	return atomic_read(&page->_refcount);
}

static inline int folio_ref_count(const struct folio *folio)
{
	return page_ref_count(&folio->page);
}

static inline void set_page_count(struct page *page, int v)
{
	atomic_set(&page->_refcount, v);
}

/* page_ref_add, page_ref_sub, page_ref_inc, folio_ref_add, folio_ref_sub inlined */
static inline void folio_ref_inc(struct folio *folio)
{
	atomic_inc(&folio->page._refcount);
}

static inline bool folio_try_get_rcu(struct folio *folio)
{
	return atomic_add_unless(&folio->page._refcount, 1, 0);
}
/* sizes.h inlined */
#define SZ_64K				0x00010000
#define SZ_1M				0x00100000
#include <linux/sched.h>
#include <linux/pgtable.h>

struct anon_vma_chain;
struct pt_regs;

extern unsigned long max_mapnr;

extern atomic_long_t _totalram_pages;
static inline unsigned long totalram_pages(void)
{
	return (unsigned long)atomic_long_read(&_totalram_pages);
}

extern void * high_memory;

#include <asm/page.h>
#include <asm/processor.h>

#ifndef untagged_addr
#define untagged_addr(addr) (addr)
#endif

#ifndef __pa_symbol
#define __pa_symbol(x)  __pa(RELOC_HIDE((unsigned long)(x), 0))
#endif

#ifndef page_to_virt
#define page_to_virt(x)	__va(PFN_PHYS(page_to_pfn(x)))
#endif

/* BITS_PER_LONG == 32 */
#define mm_zero_struct_page(pp)  ((void)memset((pp), 0, sizeof(struct page)))

#define DEFAULT_MAX_MAP_COUNT	(USHRT_MAX - 5)

extern int sysctl_max_map_count;

extern int sysctl_overcommit_memory;

#define nth_page(page,n) ((page) + (n))

#define PAGE_ALIGN(addr) ALIGN(addr, PAGE_SIZE)

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
#define VM_MAYSHARE	0x00000080

#define VM_GROWSDOWN	0x00000100
#define VM_PFNMAP	0x00000400

#define VM_LOCKED	0x00002000
#define VM_IO           0x00004000	

#define VM_SEQ_READ	0x00008000	
#define VM_RAND_READ	0x00010000	

#define VM_ACCOUNT	0x00100000
#define VM_SYNC		0x00800000

# define VM_SOFTDIRTY	0

#define VM_MIXEDMAP	0x10000000
#define VM_NOHUGEPAGE	0x40000000

#ifndef VM_GROWSUP
# define VM_GROWSUP	VM_NONE
#endif

#define VM_STACK_INCOMPLETE_SETUP	(VM_RAND_READ | VM_SEQ_READ)

#define VM_DATA_FLAGS_EXEC	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#ifndef VM_DATA_DEFAULT_FLAGS		
#define VM_DATA_DEFAULT_FLAGS  VM_DATA_FLAGS_EXEC
#endif

#ifndef VM_STACK_DEFAULT_FLAGS		
#define VM_STACK_DEFAULT_FLAGS VM_DATA_DEFAULT_FLAGS
#endif

#define VM_STACK	VM_GROWSDOWN

#define VM_STACK_FLAGS	(VM_STACK | VM_STACK_DEFAULT_FLAGS | VM_ACCOUNT)

#define VM_ACCESS_FLAGS (VM_READ | VM_WRITE | VM_EXEC)

#define VM_INIT_DEF_MASK	VM_NOHUGEPAGE

extern pgprot_t protection_map[16];

#define FAULT_FLAG_DEFAULT  (FAULT_FLAG_ALLOW_RETRY | \
			     FAULT_FLAG_KILLABLE)

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
	int (*may_split)(struct vm_area_struct *area, unsigned long addr);
	vm_fault_t (*fault)(struct vm_fault *vmf);
	vm_fault_t (*page_mkwrite)(struct vm_fault *vmf);
};

/* vma_init inlined at kernel/fork.c - single caller */

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

static inline int put_page_testzero(struct page *page)
{
	VM_BUG_ON_PAGE(page_ref_count(page) == 0, page);
	return atomic_dec_and_test(&page->_refcount);
}

/* page_mapcount inlined at mm/filemap.c - single caller */

void __put_page(struct page *page);

typedef void compound_page_dtor(struct page *);

enum compound_dtor_id {
	NULL_COMPOUND_DTOR,
	COMPOUND_PAGE_DTOR,
	NR_COMPOUND_DTORS,
};
extern compound_page_dtor * const compound_page_dtors[NR_COMPOUND_DTORS];

static inline void destroy_compound_page(struct page *page)
{
	VM_BUG_ON_PAGE(page[1].compound_dtor >= NR_COMPOUND_DTORS, page);
	compound_page_dtors[page[1].compound_dtor](page);
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

/* thp_nr_pages inlined at mm/swap.c - single caller */

static inline pte_t maybe_mkwrite(pte_t pte, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pte = pte_mkwrite(pte);
	return pte;
}

#define SECTIONS_PGOFF		((sizeof(unsigned long)*8) - SECTIONS_WIDTH)
#define NODES_PGOFF		(SECTIONS_PGOFF - NODES_WIDTH)
#define ZONES_PGOFF		(NODES_PGOFF - ZONES_WIDTH)

#define NODES_PGSHIFT		(NODES_PGOFF * (NODES_WIDTH != 0))
#define ZONES_PGSHIFT		(ZONES_PGOFF * (ZONES_WIDTH != 0))

#define ZONES_MASK		((1UL << ZONES_WIDTH) - 1)
#define NODES_MASK		((1UL << NODES_WIDTH) - 1)

static inline enum zone_type page_zonenum(const struct page *page)
{
	return (page->flags >> ZONES_PGSHIFT) & ZONES_MASK;
}

static inline enum zone_type folio_zonenum(const struct folio *folio)
{
	return page_zonenum(&folio->page);
}

static inline void folio_get(struct folio *folio)
{
	VM_BUG_ON_FOLIO((unsigned int) folio_ref_count(folio) + 127u <= 127u, folio);
	folio_ref_inc(folio);
}

bool __must_check try_grab_page(struct page *page, unsigned int flags);

static inline void folio_put(struct folio *folio)
{
	if (put_page_testzero(&folio->page))
		__put_page(&folio->page);
}

static inline void put_page(struct page *page)
{
	folio_put(page_folio(page));
}

static inline bool is_cow_mapping(vm_flags_t flags)
{
	return (flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;
}

static inline int page_to_nid(const struct page *page)
{
	struct page *p = (struct page *)page;
	return (PF_POISONED_CHECK(p)->flags >> NODES_PGSHIFT) & NODES_MASK;
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

static inline long folio_nr_pages(struct folio *folio)
{
	return compound_nr(&folio->page);
}

static inline size_t folio_size(struct folio *folio)
{
	return PAGE_SIZE << folio_order(folio);
}

#include <linux/vmstat.h>

#define page_address(page) page_to_virt(page)

/* page_is_pfmemalloc inlined at mm/slub.c - single caller */
/* set_page_pfmemalloc, clear_page_pfmemalloc inlined at single call site */

#define offset_in_page(p)	((unsigned long)(p) & ~PAGE_MASK)

struct page *vm_normal_page(struct vm_area_struct *vma, unsigned long addr,
			     pte_t pte);

void unmap_vmas(struct mmu_gather *tlb, struct vm_area_struct *start_vma,
		unsigned long start, unsigned long end);

void free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
		unsigned long end, unsigned long floor, unsigned long ceiling);

extern vm_fault_t handle_mm_fault(struct vm_area_struct *vma,
				  unsigned long address, unsigned int flags,
				  struct pt_regs *regs);

long get_user_pages_remote(struct mm_struct *mm,
			    unsigned long start, unsigned long nr_pages,
			    unsigned int gup_flags, struct page **pages,
			    struct vm_area_struct **vmas, int *locked);
/* pin_user_pages_remote, get_user_pages, pin_user_pages, get_user_pages_unlocked,
   pin_user_pages_unlocked, get_user_pages_fast, pin_user_pages_fast removed - unused */

/* folio_mark_dirty inlined from mm/page-writeback.c - stub */
static inline bool folio_mark_dirty(struct folio *folio) { return true; }
bool set_page_dirty(struct page *page);

extern unsigned long move_page_tables(struct vm_area_struct *vma,
		unsigned long old_addr, struct vm_area_struct *new_vma,
		unsigned long new_addr, unsigned long len,
		bool need_rmap_locks);

extern pte_t *__get_locked_pte(struct mm_struct *mm, unsigned long addr,
			       spinlock_t **ptl);
static inline pte_t *get_locked_pte(struct mm_struct *mm, unsigned long addr,
				    spinlock_t **ptl)
{
	pte_t *ptep;
	__cond_lock(*ptl, ptep = __get_locked_pte(mm, addr, ptl));
	return ptep;
}

/* mm_pgtables_bytes_init inlined at kernel/fork.c - single caller */
/* mm_inc_nr_ptes and mm_dec_nr_ptes inlined at mm/memory.c - single caller each */

int __pte_alloc(struct mm_struct *mm, pmd_t *pmd);
int __pte_alloc_kernel(pmd_t *pmd);

/* pgd_none/p4d_none/pud_none always return 0 - folded paging */
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

/* USE_SPLIT_PTE_PTLOCKS is 0 (NR_CPUS=1 < SPLIT_PTLOCK_CPUS=4) */
/* Use simple page_table_lock */
static inline spinlock_t *pte_lockptr(struct mm_struct *mm, pmd_t *pmd)
{
	return &mm->page_table_lock;
}

/* pgtable_pte_page_ctor inlined at arch/x86/include/asm/pgalloc.h - single caller */

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

extern void __init pagecache_init(void);
extern void free_initmem(void);

extern void reserve_bootmem_region(phys_addr_t start, phys_addr_t end);

/* free_reserved_page inlined at mm/page_alloc.c - single caller */

void free_area_init(unsigned long *max_zone_pfn);
extern void mem_init(void);
extern void __init mmap_init(void);

extern void setup_per_cpu_pageset(void);

void vma_interval_tree_insert(struct vm_area_struct *node,
			      struct rb_root_cached *root);
void vma_interval_tree_remove(struct vm_area_struct *node,
			      struct rb_root_cached *root);

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
extern int insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void unlink_file_vma(struct vm_area_struct *);
extern void exit_mmap(struct mm_struct *);

extern int set_mm_exe_file(struct mm_struct *mm, struct file *new_exe_file);

extern unsigned long get_unmapped_area(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);

extern unsigned long do_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot, unsigned long flags,
	unsigned long pgoff, unsigned long *populate, struct list_head *uf);
extern int do_munmap(struct mm_struct *, unsigned long, size_t,
		     struct list_head *uf);

extern int __mm_populate(unsigned long addr, unsigned long len,
			 int ignore_errors);
static inline void mm_populate(unsigned long addr, unsigned long len)
{

	(void) __mm_populate(addr, len, 1);
}

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

extern unsigned long stack_guard_gap;

extern int expand_stack(struct vm_area_struct *vma, unsigned long address);

extern struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr);

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
	unsigned long vm_end = vma->vm_end;

	if (vma->vm_flags & VM_GROWSUP) {
		vm_end += stack_guard_gap;
		if (vm_end < vma->vm_end)
			vm_end = -PAGE_SIZE;
	}
	return vm_end;
}

static inline unsigned long vma_pages(struct vm_area_struct *vma)
{
	return (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
}

pgprot_t vm_get_page_prot(unsigned long vm_flags);

struct vm_area_struct *find_extend_vma(struct mm_struct *, unsigned long addr);

#define FOLL_WRITE	0x01
#define FOLL_TOUCH	0x02
#define FOLL_GET	0x04
#define FOLL_FORCE	0x10
#define FOLL_TRIED	0x800
#define FOLL_REMOTE	0x2000
#define FOLL_COW	0x4000

/* vm_fault_to_errno inlined at mm/gup.c - single caller */

extern void init_mem_debugging_and_hardening(void);

DECLARE_STATIC_KEY_MAYBE(CONFIG_INIT_ON_ALLOC_DEFAULT_ON, init_on_alloc);
DECLARE_STATIC_KEY_MAYBE(CONFIG_INIT_ON_FREE_DEFAULT_ON, init_on_free);

/* MAX_NUMNODES == 1, always inline */

#endif
