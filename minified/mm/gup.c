#include "internal.h"

static struct page *follow_page_pte(struct vm_area_struct *vma,
				    unsigned long address, unsigned int flags)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	struct page *page;
	spinlock_t *ptl;
	pte_t *ptep, pte;

	pgd = pgd_offset(mm, address);
	p4d = p4d_offset(pgd, address);
	pud = pud_offset(p4d, address);

	pmd = pmd_offset(pud, address);
	if (pmd_none(READ_ONCE(*pmd)) || !pmd_present(*pmd))
		return NULL;
	if (unlikely(pmd_bad(*pmd)))
		return NULL;

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	pte = *ptep;
	if (!pte_present(pte)) {
		pte_unmap_unlock(ptep, ptl);
		return NULL;
	}

	if ((flags & FOLL_WRITE) &&
	    !(pte_write(pte) ||
	      ((flags & FOLL_FORCE) && (flags & FOLL_COW) && pte_dirty(pte)))) {
		pte_unmap_unlock(ptep, ptl);
		return NULL;
	}

	page = vm_normal_page(vma, address, pte);
	if (unlikely(!page)) {
		if (is_zero_pfn(pte_pfn(pte)))
			page = pte_page(pte);
		else {
			pte_unmap_unlock(ptep, ptl);
			return ERR_PTR(-EEXIST);
		}
	}

	if (unlikely(!page_folio(page) ||
		     folio_ref_count(page_folio(page)) <= 0)) {
		pte_unmap_unlock(ptep, ptl);
		return ERR_PTR(-ENOMEM);
	}
	folio_ref_inc(page_folio(page));

	pte_unmap_unlock(ptep, ptl);
	return page;
}

/*
 * Simplified get_user_pages_remote: always fetches exactly 1 page for exec.
 * vmas and locked parameters are ignored (always NULL from our sole caller).
 */
long get_user_pages_remote(struct mm_struct *mm, unsigned long start,
			   unsigned long nr_pages, unsigned int gup_flags,
			   struct page **pages, struct vm_area_struct **vmas,
			   int *locked)
{
	struct vm_area_struct *vma;
	struct page *page;
	unsigned int foll_flags;
	vm_fault_t fault_ret;

	start = untagged_addr(start);
	gup_flags |= FOLL_GET | FOLL_TOUCH | FOLL_REMOTE;

	vma = find_extend_vma(mm, start);
	if (!vma)
		return -EFAULT;

	if (vma->vm_flags & (VM_IO | VM_PFNMAP))
		return -EFAULT;

	if ((gup_flags & FOLL_WRITE) && !(vma->vm_flags & VM_WRITE)) {
		if (!(gup_flags & FOLL_FORCE) || !is_cow_mapping(vma->vm_flags))
			return -EFAULT;
	}

	foll_flags = gup_flags;
retry:
	cond_resched();

	page = follow_page_pte(vma, start, foll_flags);
	if (!page) {
		unsigned int fault_flags = FAULT_FLAG_WRITE | FAULT_FLAG_REMOTE;

		fault_ret = handle_mm_fault(vma, start, fault_flags, NULL);
		if (fault_ret & VM_FAULT_ERROR)
			return -EFAULT;

		if ((fault_ret & VM_FAULT_WRITE) && !(vma->vm_flags & VM_WRITE))
			foll_flags |= FOLL_COW;

		goto retry;
	}
	if (IS_ERR(page))
		return PTR_ERR(page);

	if (pages)
		pages[0] = page;
	return 1;
}
