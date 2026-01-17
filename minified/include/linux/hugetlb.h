/* Minimal hugetlb.h stub - CONFIG_HUGETLB_PAGE is disabled */
#ifndef _LINUX_HUGETLB_H
#define _LINUX_HUGETLB_H

#include <linux/mm_types.h>
#include <linux/err.h>

/* is_hugepd, hugepd_t, __hugepd removed - never used */

/* pgd_huge removed - never called */
#ifndef p4d_huge
#define p4d_huge(x)	0
#endif

/* is_file_hugepages removed - never called */

struct hstate {};

/* Functions called in the codebase */
static inline void hugetlb_count_init(struct mm_struct *mm)
{
}

/* follow_huge_addr removed - call site in gup.c was removed */

static inline struct hstate *hstate_vma(struct vm_area_struct *vma)
{
	return NULL;
}

static inline unsigned int huge_page_shift(struct hstate *h)
{
	return PAGE_SHIFT;
}

/* All other hugetlb_* functions removed - never called */

#endif
