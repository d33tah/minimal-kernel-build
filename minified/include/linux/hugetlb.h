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

/* struct hstate removed - never used, hugetlb is disabled */

/* follow_huge_addr removed - call site in gup.c was removed */

/* All other hugetlb_* functions removed - never called */

#endif
