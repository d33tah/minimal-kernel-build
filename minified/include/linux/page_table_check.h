
#ifndef __LINUX_PAGE_TABLE_CHECK_H
#define __LINUX_PAGE_TABLE_CHECK_H

static inline void page_table_check_pte_clear(struct mm_struct *mm,
					      unsigned long addr, pte_t pte)
{
}

static inline void page_table_check_pte_set(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep,
					    pte_t pte)
{
}

/* Removed: page_table_check_alloc, page_table_check_free, page_table_check_pmd_set
 * - Never called */

#endif
