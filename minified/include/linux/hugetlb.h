/* Minimal hugetlb.h - all stubs */
#ifndef _LINUX_HUGETLB_H
#define _LINUX_HUGETLB_H

#include <linux/mm_types.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

struct mmu_gather;
struct vm_area_struct;
struct mm_struct;
struct page;

#ifndef is_hugepd
typedef struct { unsigned long pd; } hugepd_t;
#define is_hugepd(hugepd) (0)
#define __hugepd(x) ((hugepd_t) { (x) })
#endif



static inline unsigned long hugetlb_total_pages(void)
{
	return 0;
}



static inline long follow_hugetlb_page(struct mm_struct *mm,
			struct vm_area_struct *vma, struct page **pages,
			struct vm_area_struct **vmas, unsigned long *position,
			unsigned long *nr_pages, long i, unsigned int flags,
			int *nonblocking)
{
	BUG();
	return 0;
}

static inline struct page *follow_huge_addr(struct mm_struct *mm,
					unsigned long address, int write)
{
	return ERR_PTR(-EINVAL);
}


static inline int move_hugetlb_page_tables(struct vm_area_struct *vma,
					   struct vm_area_struct *new_vma,
					   unsigned long old_addr,
					   unsigned long new_addr,
					   unsigned long len)
{
	BUG();
	return 0;
}


static inline struct page *follow_huge_pd(struct vm_area_struct *vma,
				unsigned long address, hugepd_t hpd, int flags,
				int pdshift)
{
	return NULL;
}

static inline struct page *follow_huge_pmd(struct mm_struct *mm,
				unsigned long address, pmd_t *pmd, int flags)
{
	return NULL;
}

static inline struct page *follow_huge_pud(struct mm_struct *mm,
				unsigned long address, pud_t *pud, int flags)
{
	return NULL;
}

static inline struct page *follow_huge_pgd(struct mm_struct *mm,
				unsigned long address, pgd_t *pgd, int flags)
{
	return NULL;
}


static inline int pmd_huge(pmd_t pmd)
{
	return 0;
}

static inline int pud_huge(pud_t pud)
{
	return 0;
}

static inline int is_hugepage_only_range(struct mm_struct *mm,
					unsigned long addr, unsigned long len)
{
	return 0;
}

static inline void hugetlb_free_pgd_range(struct mmu_gather *tlb,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	BUG();
}


static inline pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr,
					unsigned long sz)
{
	return NULL;
}



static inline void __unmap_hugepage_range_final(struct mmu_gather *tlb,
			struct vm_area_struct *vma, unsigned long start,
			unsigned long end, struct page *ref_page,
			zap_flags_t zap_flags)
{
	BUG();
}

static inline vm_fault_t hugetlb_fault(struct mm_struct *mm,
			struct vm_area_struct *vma, unsigned long address,
			unsigned int flags)
{
	BUG();
	return 0;
}


#ifndef pgd_huge
#define pgd_huge(x)	0
#endif
#ifndef p4d_huge
#define p4d_huge(x)	0
#endif


#define HUGETLB_ANON_FILE "anon_hugepage"

enum {
	 
	HUGETLB_SHMFS_INODE     = 1,
	 
	HUGETLB_ANONHUGE_INODE  = 2,
};


#define is_file_hugepages(file)			false
static inline struct file *
hugetlb_file_setup(const char *name, size_t size, vm_flags_t acctflag,
		int creat_flags, int page_size_log)
{
	return ERR_PTR(-ENOSYS);
}

struct hstate {};

static inline struct hstate *hstate_file(struct file *f)
{
	return NULL;
}

static inline struct hstate *hstate_sizelog(int page_size_log)
{
	return NULL;
}

static inline struct hstate *hstate_vma(struct vm_area_struct *vma)
{
	return NULL;
}


static inline unsigned long huge_page_size(struct hstate *h)
{
	return PAGE_SIZE;
}


static inline unsigned int huge_page_shift(struct hstate *h)
{
	return PAGE_SHIFT;
}

static inline spinlock_t *huge_pte_lockptr(struct hstate *h,
					   struct mm_struct *mm, pte_t *pte)
{
	return &mm->page_table_lock;
}

static inline void hugetlb_count_init(struct mm_struct *mm)
{
}

#ifndef __HAVE_ARCH_FLUSH_HUGETLB_TLB_RANGE
#define flush_hugetlb_tlb_range(vma, addr, end)	flush_tlb_range(vma, addr, end)
#endif

#endif  
