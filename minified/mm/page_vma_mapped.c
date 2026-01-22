#include <linux/mm.h>
#include <linux/rmap.h>
#include <linux/hugetlb.h>
#include <linux/swap.h>
#include <linux/swapops.h>

#include "internal.h"

/* not_found removed - inlined into callers (~5 LOC) */

static bool map_pte(struct page_vma_mapped_walk *pvmw)
{
	pvmw->pte = pte_offset_map(pvmw->pmd, pvmw->address);
	if (!(pvmw->flags & PVMW_SYNC)) {
		if (is_swap_pte(*pvmw->pte))
			return false;
		if (!pte_present(*pvmw->pte))
			return false;
	}
	pvmw->ptl = pte_lockptr(pvmw->vma->vm_mm, pvmw->pmd);
	spin_lock(pvmw->ptl);
	return true;
}

static bool check_pte(struct page_vma_mapped_walk *pvmw)
{
	unsigned long pfn;

	if (is_swap_pte(*pvmw->pte))
		return false;
	if (!pte_present(*pvmw->pte))
		return false;

	pfn = pte_pfn(*pvmw->pte);
	return (pfn - pvmw->pfn) < pvmw->nr_pages;
}

/* Removed: check_pmd, step_forward - inlined */

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
		if (!map_pte(pvmw))
			goto next_pte;
this_pte:
		if (check_pte(pvmw))
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
