#ifndef _LINUX_HUGE_MM_H
#define _LINUX_HUGE_MM_H

#include <linux/sched/coredump.h>
#include <linux/mm_types.h>

#include <linux/fs.h>  

static inline bool folio_test_pmd_mappable(struct folio *folio)
{
	return false;
}


static inline void prep_transhuge_page(struct page *page) {}

static inline int split_huge_page(struct page *page)
{
	return 0;
}

static inline bool is_huge_zero_page(struct page *page)
{
	return false;
}

static inline void mm_put_huge_zero_page(struct mm_struct *mm)
{
	return;
}

#endif
