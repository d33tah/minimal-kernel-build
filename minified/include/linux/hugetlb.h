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



#ifndef pgd_huge
#define pgd_huge(x)	0
#endif
#ifndef p4d_huge
#define p4d_huge(x)	0
#endif


struct hstate {};

static inline struct hstate *hstate_vma(struct vm_area_struct *vma)
{
	return NULL;
}


static inline unsigned int huge_page_shift(struct hstate *h)
{
	return PAGE_SHIFT;
}

static inline void hugetlb_count_init(struct mm_struct *mm)
{
}



static inline __init void hugetlb_cma_reserve(int order)
{
}

#endif
