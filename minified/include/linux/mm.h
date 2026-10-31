
#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <linux/gfp.h>
#include <linux/mmap_lock.h>

static inline int page_ref_count(const struct page *page)
{
	return atomic_read(&page->_refcount);
}

static inline int folio_ref_count(const struct folio *folio)
{
	return page_ref_count(&folio->page);
}

/* Removed: page_count, update_hiwater_rss - 0-caller orphans */

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
#include <linux/pgtable.h>

struct pt_regs;

extern unsigned long max_mapnr;

extern atomic_long_t _totalram_pages;
static inline unsigned long totalram_pages(void)
{
	return (unsigned long)atomic_long_read(&_totalram_pages);
}

static inline void totalram_pages_add(long count)
{
	atomic_long_add(count, &_totalram_pages);
}

extern void *high_memory;

#define untagged_addr(addr) (addr)

#define page_to_virt(x) __va(PFN_PHYS(page_to_pfn(x)))

/* BITS_PER_LONG == 32 */
#define mm_zero_struct_page(pp) ((void)memset((pp), 0, sizeof(struct page)))

#define MAPCOUNT_ELF_CORE_MARGIN (5)
#define DEFAULT_MAX_MAP_COUNT (USHRT_MAX - MAPCOUNT_ELF_CORE_MARGIN)

extern int sysctl_max_map_count;

#define nth_page(page, n) ((page) + (n))

#define PAGE_ALIGN(addr) ALIGN(addr, PAGE_SIZE)

void setup_initial_init_mm(void *start_code, void *end_code, void *end_data,
			   void *brk);

struct vm_area_struct *vm_area_alloc(struct mm_struct *);
void vm_area_free(struct vm_area_struct *);

#define VM_NONE 0x00000000

#define VM_READ 0x00000001
#define VM_WRITE 0x00000002
#define VM_EXEC 0x00000004
#define VM_SHARED 0x00000008

#define VM_MAYREAD 0x00000010
#define VM_MAYWRITE 0x00000020
#define VM_MAYEXEC 0x00000040

#define VM_GROWSDOWN 0x00000100
/* VM_UFFD_WP (0x1000) removed - unused */

#define VM_LOCKED 0x00002000

#define VM_ACCOUNT 0x00100000

#define VM_NOHUGEPAGE 0x40000000

#define TASK_EXEC ((current->personality & READ_IMPLIES_EXEC) ? VM_EXEC : 0)

#define VM_DATA_FLAGS_TSK_EXEC \
	(VM_READ | VM_WRITE | TASK_EXEC | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#define VM_STACK_DEFAULT_FLAGS VM_DATA_DEFAULT_FLAGS

#define VM_STACK VM_GROWSDOWN

#define VM_STACK_FLAGS (VM_STACK | VM_STACK_DEFAULT_FLAGS | VM_ACCOUNT)

#define VM_ACCESS_FLAGS (VM_READ | VM_WRITE | VM_EXEC)

#define VM_INIT_DEF_MASK VM_NOHUGEPAGE

extern pgprot_t protection_map[16];

#define FAULT_FLAG_DEFAULT (FAULT_FLAG_ALLOW_RETRY)

struct vm_fault {
	const struct {
		struct vm_area_struct *vma;
		gfp_t gfp_mask;
		pgoff_t pgoff;
		unsigned long address;
	};
	enum fault_flag flags;
	pmd_t *pmd;
	pud_t *pud;
	union {
		pte_t orig_pte;
	};
	struct page *cow_page, *page;
	pte_t *pte;
	spinlock_t *ptl;
	pgtable_t prealloc_pte;
};

struct vm_operations_struct {
	vm_fault_t (*fault)(struct vm_fault *vmf);
};

static inline void vma_init(struct vm_area_struct *vma, struct mm_struct *mm)
{
	static const struct vm_operations_struct dummy_vm_ops = {};

	memset(vma, 0, sizeof(*vma));
	vma->vm_mm = mm;
	vma->vm_ops = &dummy_vm_ops;
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

#include <linux/sched/coredump.h>

#include <linux/fs.h>

static inline int put_page_testzero(struct page *page)
{
	return page_ref_dec_and_test(page);
}

static inline int folio_put_testzero(struct folio *folio)
{
	return put_page_testzero(&folio->page);
}

static inline void page_mapcount_reset(struct page *page)
{
	atomic_set(&(page)->_mapcount, -1);
}

static inline struct folio *virt_to_folio(const void *x)
{
	struct page *page = virt_to_page(x);

	return page_folio(page);
}

void __put_page(struct page *page);

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

static inline pte_t maybe_mkwrite(pte_t pte, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pte = pte_mkwrite(pte);
	return pte;
}

#define SECTIONS_PGOFF ((sizeof(unsigned long) * 8) - SECTIONS_WIDTH)
#define NODES_PGOFF (SECTIONS_PGOFF - NODES_WIDTH)
#define ZONES_PGOFF (NODES_PGOFF - ZONES_WIDTH)

#define NODES_PGSHIFT (NODES_PGOFF * (NODES_WIDTH != 0))
#define ZONES_PGSHIFT (ZONES_PGOFF * (ZONES_WIDTH != 0))

#define ZONES_MASK ((1UL << ZONES_WIDTH) - 1)
#define NODES_MASK ((1UL << NODES_WIDTH) - 1)

static inline enum zone_type page_zonenum(const struct page *page)
{
	ASSERT_EXCLUSIVE_BITS(page->flags, ZONES_MASK << ZONES_PGSHIFT);
	return (page->flags >> ZONES_PGSHIFT) & ZONES_MASK;
}

static inline void folio_get(struct folio *folio)
{
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

/*
 * NODES_SHIFT is unconditionally 0 in this tree (linux/numa.h), so
 * NODE_NOT_IN_PAGE_FLAGS is never defined (page-flags-layout.h) and the
 * out-of-line extern arm was statically dead; the node id always lives in
 * page->flags, so page_to_nid() is always this inline.
 */
static inline int page_to_nid(const struct page *page)
{
	struct page *p = (struct page *)page;

	return (PF_POISONED_CHECK(p)->flags >> NODES_PGSHIFT) & NODES_MASK;
}

static inline int folio_nid(const struct folio *folio)
{
	return page_to_nid(&folio->page);
}

static inline struct zone *page_zone(const struct page *page)
{
	return &NODE_DATA(page_to_nid(page))->node_zones[page_zonenum(page)];
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

static __always_inline void *lowmem_page_address(const struct page *page)
{
	return page_to_virt(page);
}

#define page_address(page) lowmem_page_address(page)

static inline void *folio_address(const struct folio *folio)
{
	return page_address(&folio->page);
}

#define offset_in_page(p) ((unsigned long)(p) & ~PAGE_MASK)

struct page *vm_normal_page(struct vm_area_struct *vma, unsigned long addr,
			    pte_t pte);

extern vm_fault_t handle_mm_fault(struct vm_area_struct *vma,
				  unsigned long address, unsigned int flags,
				  struct pt_regs *regs);
long get_user_pages_remote(struct mm_struct *mm, unsigned long start,
			   unsigned long nr_pages, unsigned int gup_flags,
			   struct page **pages);
/* pin_user_pages_remote, get_user_pages, pin_user_pages, get_user_pages_unlocked,
   pin_user_pages_unlocked, get_user_pages_fast, pin_user_pages_fast removed - unused */

/* dec_mm_counter removed - 0 callers (dec_mm_counter_fast macro never invoked) */

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

static inline pmd_t *pmd_alloc(struct mm_struct *mm, pud_t *pud,
			       unsigned long address)
{
	return pmd_offset(pud, address);
}

static inline spinlock_t *pte_lockptr(struct mm_struct *mm, pmd_t *pmd)
{
	return &mm->page_table_lock;
}

static inline bool pgtable_pte_page_ctor(struct page *page)
{
	__SetPageTable(page);
	return true;
}

static inline void pgtable_pte_page_dtor(struct page *page)
{
	__ClearPageTable(page);
}

#define pte_offset_map_lock(mm, pmd, address, ptlp)          \
	({                                                   \
		spinlock_t *__ptl = pte_lockptr(mm, pmd);    \
		pte_t *__pte = pte_offset_map(pmd, address); \
		*(ptlp) = __ptl;                             \
		spin_lock(__ptl);                            \
		__pte;                                       \
	})

#define pte_unmap_unlock(pte, ptl) \
	do {                       \
		spin_unlock(ptl);  \
		pte_unmap(pte);    \
	} while (0)

#define pte_alloc(mm, pmd) (unlikely(pmd_none(*(pmd))) && __pte_alloc(mm, pmd))

static inline spinlock_t *pmd_lockptr(struct mm_struct *mm, pmd_t *pmd)
{
	return &mm->page_table_lock;
}

static inline spinlock_t *pmd_lock(struct mm_struct *mm, pmd_t *pmd)
{
	spinlock_t *ptl = pmd_lockptr(mm, pmd);
	spin_lock(ptl);
	return ptl;
}

extern void reserve_bootmem_region(phys_addr_t start, phys_addr_t end);

void free_area_init(unsigned long *max_zone_pfn);
extern void mem_init(void);

extern int __vm_enough_memory(struct mm_struct *mm, long pages,
			      int cap_sys_admin);
extern int insert_vm_struct(struct mm_struct *, struct vm_area_struct *);

extern unsigned long get_unmapped_area(struct file *, unsigned long,
				       unsigned long, unsigned long,
				       unsigned long);
extern unsigned long mmap_region(struct file *file, unsigned long addr,
				 unsigned long len, vm_flags_t vm_flags,
				 unsigned long pgoff);
extern unsigned long do_mmap(struct file *file, unsigned long addr,
			     unsigned long len, unsigned long prot,
			     unsigned long flags, unsigned long pgoff,
			     unsigned long *populate);
extern int __do_munmap(struct mm_struct *, unsigned long, size_t);

extern int __must_check vm_brk_flags(unsigned long, unsigned long,
				     unsigned long);
extern unsigned long __must_check vm_mmap(struct file *, unsigned long,
					  unsigned long, unsigned long,
					  unsigned long, unsigned long);

extern unsigned long stack_guard_gap;
extern int expand_stack(struct vm_area_struct *vma, unsigned long address);
extern struct vm_area_struct *find_vma(struct mm_struct *mm,
				       unsigned long addr);

static inline struct vm_area_struct *
find_vma_intersection(struct mm_struct *mm, unsigned long start_addr,
		      unsigned long end_addr)
{
	struct vm_area_struct *vma = find_vma(mm, start_addr);

	if (vma && end_addr <= vma->vm_start)
		vma = NULL;
	return vma;
}

static inline unsigned long vma_pages(struct vm_area_struct *vma)
{
	return (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
}

pgprot_t vm_get_page_prot(unsigned long vm_flags);
/* Removed: vma_set_page_prot - never called */

struct vm_area_struct *find_extend_vma(struct mm_struct *, unsigned long addr);

#define FOLL_WRITE 0x01
#define FOLL_TOUCH 0x02
#define FOLL_GET 0x04
#define FOLL_FORCE 0x10
#define FOLL_REMOTE 0x2000
#define FOLL_COW 0x4000
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

/* Inlined from elf-randomize.h */

#endif
