#include <linux/mm.h>
#include <linux/rmap.h>
#include <linux/hugetlb.h>
#include <linux/swap.h>
#include <linux/swapops.h>

#include "internal.h"

/* Removed: not_found, map_pte, check_pte, check_pmd, step_forward - inlined */

bool page_vma_mapped_walk(struct page_vma_mapped_walk *pvmw)
{
	struct vm_area_struct *vma = pvmw->vma;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long end;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t pmde;

	if (pvmw->pmd && !pvmw->pte) {
		page_vma_mapped_walk_done(pvmw);
		return false;
	}

	end = vma_address_end(pvmw);
	if (pvmw->pte)
		goto next_pte;
restart:
	do {
		pgd = pgd_offset(mm, pvmw->address);
		p4d = p4d_offset(pgd, pvmw->address);
		pud = pud_offset(p4d, pvmw->address);

		pvmw->pmd = pmd_offset(pud, pvmw->address);

		pmde = READ_ONCE(*pvmw->pmd);

		if (!pmd_present(pmde)) {
			/* Inlined step_forward(pvmw, PMD_SIZE) */
			pvmw->address = (pvmw->address + PMD_SIZE) &
					~(PMD_SIZE - 1);
			if (!pvmw->address)
				pvmw->address = ULONG_MAX;
			continue;
		}
		/* map_pte inlined - single caller */
		pvmw->pte = pte_offset_map(pvmw->pmd, pvmw->address);
		if (!(pvmw->flags & PVMW_SYNC)) {
			if (is_swap_pte(*pvmw->pte) || !pte_present(*pvmw->pte))
				goto next_pte;
		}
		pvmw->ptl = pte_lockptr(pvmw->vma->vm_mm, pvmw->pmd);
		spin_lock(pvmw->ptl);
this_pte:
		/* check_pte inlined - single caller */
		if (!is_swap_pte(*pvmw->pte) && pte_present(*pvmw->pte) &&
		    (pte_pfn(*pvmw->pte) - pvmw->pfn) < pvmw->nr_pages)
			return true;
next_pte:
		do {
			pvmw->address += PAGE_SIZE;
			if (pvmw->address >= end) {
				page_vma_mapped_walk_done(pvmw);
				return false;
			}

			if ((pvmw->address & (PMD_SIZE - PAGE_SIZE)) == 0) {
				if (pvmw->ptl) {
					spin_unlock(pvmw->ptl);
					pvmw->ptl = NULL;
				}
				pte_unmap(pvmw->pte);
				pvmw->pte = NULL;
				goto restart;
			}
			pvmw->pte++;
			if ((pvmw->flags & PVMW_SYNC) && !pvmw->ptl) {
				pvmw->ptl = pte_lockptr(mm, pvmw->pmd);
				spin_lock(pvmw->ptl);
			}
		} while (pte_none(*pvmw->pte));

		if (!pvmw->ptl) {
			pvmw->ptl = pte_lockptr(mm, pvmw->pmd);
			spin_lock(pvmw->ptl);
		}
		goto this_pte;
	} while (pvmw->address < end);

	return false;
}

/* page_mapped_in_vma removed - never called */
