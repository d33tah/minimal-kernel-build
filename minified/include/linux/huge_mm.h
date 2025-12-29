#ifndef _LINUX_HUGE_MM_H
#define _LINUX_HUGE_MM_H
#include <linux/sched/coredump.h>
#include <linux/mm_types.h>
#include <linux/fs.h>
#define HPAGE_PMD_ORDER (HPAGE_PMD_SHIFT - PAGE_SHIFT)
#define HPAGE_PMD_NR (1<<HPAGE_PMD_ORDER)

#define HPAGE_PMD_SHIFT ({ BUILD_BUG(); 0; })
#define HPAGE_PMD_MASK ({ BUILD_BUG(); 0; })
#define HPAGE_PMD_SIZE ({ BUILD_BUG(); 0; })

#define HPAGE_PUD_SHIFT ({ BUILD_BUG(); 0; })
#define HPAGE_PUD_MASK ({ BUILD_BUG(); 0; })
#define HPAGE_PUD_SIZE ({ BUILD_BUG(); 0; })

static inline bool folio_test_pmd_mappable(struct folio *folio)
{
	return false;
}


static inline bool transparent_hugepage_active(struct vm_area_struct *vma)
{
	return false;
}


static inline int split_huge_page(struct page *page)
{
	return 0;
}

static inline int is_swap_pmd(pmd_t pmd)
{
	return 0;
}

#endif  
