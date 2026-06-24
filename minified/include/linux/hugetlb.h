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



struct hstate {};

static inline struct hstate *hstate_vma(struct vm_area_struct *vma)
{
	return NULL;
}


static inline unsigned int huge_page_shift(struct hstate *h)
{
	return PAGE_SHIFT;
}

#endif
