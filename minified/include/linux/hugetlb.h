/* Minimal hugetlb.h - all stubs */
#ifndef _LINUX_HUGETLB_H
#define _LINUX_HUGETLB_H

#include <linux/mm_types.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

struct ctl_table;
struct user_struct;
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



static inline int is_hugepage_only_range(struct mm_struct *mm,
					unsigned long addr, unsigned long len)
{
	return 0;
}

static inline pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr,
					unsigned long sz)
{
	return NULL;
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


#ifdef HAVE_ARCH_HUGETLB_UNMAPPED_AREA
unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
					unsigned long len, unsigned long pgoff,
					unsigned long flags);
#endif  

unsigned long
generic_hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
				  unsigned long len, unsigned long pgoff,
				  unsigned long flags);

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



static inline __init void hugetlb_cma_reserve(int order)
{
}


#ifndef __HAVE_ARCH_FLUSH_HUGETLB_TLB_RANGE
#define flush_hugetlb_tlb_range(vma, addr, end)	flush_tlb_range(vma, addr, end)
#endif

#endif  
