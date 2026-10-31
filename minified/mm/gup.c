
#include <linux/swap.h>

static struct page *no_page_table(void)
{
	/* FOLL_DUMP never set -> always returns NULL */
	return NULL;
}

static int follow_pfn_pte(struct vm_area_struct *vma, unsigned long address,
			  pte_t *pte, unsigned int flags)
{
	if (flags & FOLL_TOUCH) {
		pte_t entry = *pte;

		if (flags & FOLL_WRITE)
			entry = pte_mkdirty(entry);
		entry = pte_mkyoung(entry);

		if (!pte_same(*pte, entry)) {
			set_pte_at(vma->vm_mm, address, pte, entry);
		}
	}

	return -EEXIST;
}

static inline bool can_follow_write_pte(pte_t pte, unsigned int flags)
{
	return pte_write(pte) ||
	       ((flags & FOLL_FORCE) && (flags & FOLL_COW) && pte_dirty(pte));
}

static struct page *follow_page_pte(struct vm_area_struct *vma,
				    unsigned long address, pmd_t *pmd,
				    unsigned int flags)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;
	spinlock_t *ptl;
	pte_t *ptep, pte;
	int ret;

	if (unlikely(pmd_bad(*pmd)))
		return no_page_table();

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	pte = *ptep;
	if (!pte_present(pte))
		goto no_page;
	if ((flags & FOLL_WRITE) && !can_follow_write_pte(pte, flags)) {
		pte_unmap_unlock(ptep, ptl);
		return NULL;
	}

	page = vm_normal_page(vma, address, pte);
	if (unlikely(!page)) {
		/* FOLL_DUMP never set */
		if (is_zero_pfn(pte_pfn(pte))) {
			page = pte_page(pte);
		} else {
			ret = follow_pfn_pte(vma, address, ptep, flags);
			page = ERR_PTR(ret);
			goto out;
		}
	}

	if (!pte_write(pte) && gup_must_unshare(flags, page)) {
		page = ERR_PTR(-EMLINK);
		goto out;
	}

	{
		struct folio *folio = page_folio(page);

		if (unlikely(WARN_ON_ONCE(folio_ref_count(folio) <= 0))) {
			page = ERR_PTR(-ENOMEM);
			goto out;
		}
		/* FOLL_PIN never set on this build (no pin_user_pages callers) */
		if (flags & FOLL_GET)
			folio_ref_inc(folio);
	}
	if (flags & FOLL_TOUCH) {
		if ((flags & FOLL_WRITE) && !pte_dirty(pte) && !PageDirty(page))
			set_page_dirty(page);
	}
out:
	pte_unmap_unlock(ptep, ptl);
	return page;
no_page:
	pte_unmap_unlock(ptep, ptl);
	if (!pte_none(pte))
		return NULL;
	return no_page_table();
}

static struct page *follow_page_mask(struct vm_area_struct *vma,
				     unsigned long address, unsigned int flags)
{
	pgd_t *pgd;
	pmd_t *pmd, pmdval;
	struct mm_struct *mm = vma->vm_mm;

	pgd = pgd_offset(mm, address);

	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		return no_page_table();

	/*
	 * CONFIG_PGTABLE_LEVELS=2 (X86_32, no PAE) folds P4D/PUD/PMD onto
	 * PGD: p4d_offset/pud_offset/pmd_offset all return the same pointer
	 * cast and p4d_none/p4d_bad/pud_none/pud_bad are constant 0, so the
	 * intervening follow_p4d_mask/follow_pud_mask levels reach the PMD
	 * level directly.  Only the PMD entry read is real work.
	 */
	pmd = pmd_offset(pud_offset(p4d_offset(pgd, address), address),
			 address);

	pmdval = READ_ONCE(*pmd);
	if (pmd_none(pmdval))
		return no_page_table();
	if (!pmd_present(pmdval))
		return no_page_table();
	return follow_page_pte(vma, address, pmd, flags);
}

static int faultin_page(struct vm_area_struct *vma, unsigned long address,
			unsigned int *flags, bool unshare)
{
	unsigned int fault_flags = 0;
	vm_fault_t ret;

	/* FOLL_NOFAULT / FOLL_NOWAIT never set */
	if (*flags & FOLL_WRITE)
		fault_flags |= FAULT_FLAG_WRITE;
	if (unshare) {
		fault_flags |= FAULT_FLAG_UNSHARE;

		VM_BUG_ON(fault_flags & FAULT_FLAG_WRITE);
	}

	ret = handle_mm_fault(vma, address, fault_flags, NULL);
	if (ret & VM_FAULT_ERROR) {
		int err = vm_fault_to_errno(ret, *flags);

		if (err)
			return err;
		BUG();
	}

	/* no FAULT_FLAG_ALLOW_RETRY, so the fault is never retried */

	if ((ret & VM_FAULT_WRITE) && !(vma->vm_flags & VM_WRITE))
		*flags |= FOLL_COW;
	return 0;
}

static int check_vma_flags(struct vm_area_struct *vma, unsigned long gup_flags)
{
	vm_flags_t vm_flags = vma->vm_flags;
	int write = (gup_flags & FOLL_WRITE);

	if (vm_flags & (VM_IO | VM_PFNMAP))
		return -EFAULT;

	/* FOLL_ANON never set */

	if (write) {
		if (!(vm_flags & VM_WRITE)) {
			if (!(gup_flags & FOLL_FORCE))
				return -EFAULT;

			if (!is_cow_mapping(vm_flags))
				return -EFAULT;
		}
	} else if (!(vm_flags & VM_READ)) {
		if (!(gup_flags & FOLL_FORCE))
			return -EFAULT;

		if (!(vm_flags & VM_MAYREAD))
			return -EFAULT;
	}

	/* arch_vma_access_permitted() is constant-true (no PKU) => guard dropped. */
	return 0;
}

/*
 * The only caller is exec's get_arg_page(), which takes a reference on one
 * page of the new mm (mmap lock held by the caller, no vmas, no unlocking).
 */
long get_user_pages_remote(struct mm_struct *mm, unsigned long start,
			   unsigned long nr_pages, unsigned int gup_flags,
			   struct page **pages)
{
	long ret = 0, i = 0;
	struct vm_area_struct *vma = NULL;

	if (!nr_pages)
		return 0;

	gup_flags |= FOLL_GET | FOLL_TOUCH | FOLL_REMOTE;
	start = untagged_addr(start);

	do {
		struct page *page;
		unsigned int foll_flags = gup_flags;

		if (!vma || start >= vma->vm_end) {
			vma = find_extend_vma(mm, start);
			if (!vma) {
				ret = -EFAULT;
				goto out;
			}
			ret = check_vma_flags(vma, gup_flags);
			if (ret)
				goto out;
		}
retry:
		cond_resched();

		page = follow_page_mask(vma, start, foll_flags);
		if (!page || PTR_ERR(page) == -EMLINK) {
			ret = faultin_page(vma, start, &foll_flags,
					   PTR_ERR(page) == -EMLINK);
			if (!ret)
				goto retry;
			goto out;
		} else if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto out;
		}
		pages[i] = page;
		flush_anon_page(vma, page, start);
		flush_dcache_page(page);
		i += 1;
		start += PAGE_SIZE;
		nr_pages -= 1;
	} while (nr_pages);
out:
	return i ? i : ret;
}
