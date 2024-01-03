// SPDX-License-Identifier: GPL-2.0
/*
 *  mm/pgtable-generic.c
 *
 *  Generic pgtable methods declared in linux/pgtable.h
 *
 *  Copyright (C) 2010  Linus Torvalds
 */

#include <linux/pagemap.h>
#include <linux/hugetlb.h>
#include <linux/pgtable.h>
#include <linux/mm_inline.h>
#include <asm/tlb.h>

/*
 * If a p?d_bad entry is found while walking page tables, report
 * the error, before resetting entry to p?d_none.  Usually (but
 * very seldom) called out from the p?d_none_or_clear_bad macros.
 */

void pgd_clear_bad(pgd_t *pgd)
{
	pgd_ERROR(*pgd);
	pgd_clear(pgd);
}



/*
 * Note that the pmd variant below can't be stub'ed out just as for p4d/pud
 * above. pmd folding is special and typically pmd_* macros refer to upper
 * level even when folded
 */
void pmd_clear_bad(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_clear(pmd);
}



pte_t ptep_clear_flush(struct vm_area_struct *vma, unsigned long address,
		       pte_t *ptep)
{
	struct mm_struct *mm = (vma)->vm_mm;
	pte_t pte;
	pte = ptep_get_and_clear(mm, address, ptep);
	if (pte_accessible(mm, pte))
		flush_tlb_page(vma, address);
	return pte;
}

