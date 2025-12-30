/* Minimal hugetlb.h stub - CONFIG_HUGETLB_PAGE is disabled */
#ifndef _LINUX_HUGETLB_H
#define _LINUX_HUGETLB_H

#include <linux/mm_types.h>
#include <linux/err.h>

#ifndef is_hugepd
typedef struct { unsigned long pd; } hugepd_t;
#define is_hugepd(hugepd) (0)
#define __hugepd(x) ((hugepd_t) { (x) })
#endif

#ifndef pgd_huge
#define pgd_huge(x)	0
#endif
#ifndef p4d_huge
#define p4d_huge(x)	0
#endif

#define is_file_hugepages(file)	false

struct hstate {};

/* Functions called in the codebase */
static inline void hugetlb_count_init(struct mm_struct *mm)
{
}

static inline struct page *follow_huge_addr(struct mm_struct *mm,
					unsigned long address, int write)
{
	return ERR_PTR(-EINVAL);
}

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
