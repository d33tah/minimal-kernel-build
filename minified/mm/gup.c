#include "internal.h"

struct follow_page_context {
	unsigned int page_mask;
};

static bool __must_check try_grab_page(struct page *page, unsigned int flags)
{
	struct folio *folio = page_folio(page);
	if (WARN_ON_ONCE(folio_ref_count(folio) <= 0))
		return false;

	if (flags & FOLL_GET)
		folio_ref_inc(folio);

	return true;
}

static struct page *follow_page_mask(struct vm_area_struct *vma,
				     unsigned long address, unsigned int flags,
				     struct follow_page_context *ctx)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd, pmdval;
	struct page *page;
	spinlock_t *ptl;
	pte_t *ptep, pte;

	ctx->page_mask = 0;

	pgd = pgd_offset(mm, address);
	p4d = p4d_offset(pgd, address);
	BUILD_BUG_ON(p4d_huge(*p4d));
	pud = pud_offset(p4d, address);

	pmd = pmd_offset(pud, address);
	pmdval = READ_ONCE(*pmd);
	if (pmd_none(pmdval))
		return NULL;
	if (!pmd_present(pmdval))
		return NULL;

	if (unlikely(pmd_bad(*pmd)))
		return NULL;

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	pte = *ptep;
	if (!pte_present(pte))
		goto no_page;

	if ((flags & FOLL_WRITE) &&
	    !(pte_write(pte) ||
	      ((flags & FOLL_FORCE) && (flags & FOLL_COW) && pte_dirty(pte)))) {
		pte_unmap_unlock(ptep, ptl);
		return NULL;
	}

	page = vm_normal_page(vma, address, pte);
	if (unlikely(!page)) {
		if (is_zero_pfn(pte_pfn(pte))) {
			page = pte_page(pte);
		} else {
			if (flags & FOLL_TOUCH) {
				pte_t entry = *ptep;

				if (flags & FOLL_WRITE)
					entry = pte_mkdirty(entry);
				entry = pte_mkyoung(entry);

				if (!pte_same(*ptep, entry))
					set_pte_at(mm, address, ptep, entry);
			}
			page = ERR_PTR(-EEXIST);
			goto out;
		}
	}

	if (unlikely(!try_grab_page(page, flags))) {
		page = ERR_PTR(-ENOMEM);
		goto out;
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
	return NULL;
}

static long __get_user_pages(struct mm_struct *mm, unsigned long start,
			     unsigned long nr_pages, unsigned int gup_flags,
			     struct page **pages, struct vm_area_struct **vmas,
			     int *locked)
{
	long ret = 0, i = 0;
	struct vm_area_struct *vma = NULL;
	struct follow_page_context ctx = { 0 };

	if (!nr_pages)
		return 0;

	start = untagged_addr(start);

	do {
		struct page *page;
		unsigned int foll_flags = gup_flags;
		unsigned int page_increm;

		if (!vma || start >= vma->vm_end) {
			vma = find_extend_vma(mm, start);
			if (!vma) {
				ret = -EFAULT;
				goto out;
			}
			{
				vm_flags_t vm_flags = vma->vm_flags;
				int write = (gup_flags & FOLL_WRITE);
				if (vm_flags & (VM_IO | VM_PFNMAP)) {
					ret = -EFAULT;
					goto out;
				}
				if (write) {
					if (!(vm_flags & VM_WRITE)) {
						if (!(gup_flags & FOLL_FORCE)) {
							ret = -EFAULT;
							goto out;
						}
						if (!is_cow_mapping(vm_flags)) {
							ret = -EFAULT;
							goto out;
						}
					}
				} else if (!(vm_flags & VM_READ)) {
					if (!(gup_flags & FOLL_FORCE)) {
						ret = -EFAULT;
						goto out;
					}
					if (!(vm_flags & VM_MAYREAD)) {
						ret = -EFAULT;
						goto out;
					}
				}
			}
		}
retry:

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
		cond_resched();

		page = follow_page_mask(vma, start, foll_flags, &ctx);
		if (!page) {
			unsigned int fault_flags = 0;
			vm_fault_t fault_ret;

			if (foll_flags & FOLL_WRITE)
				fault_flags |= FAULT_FLAG_WRITE;
			if (foll_flags & FOLL_REMOTE)
				fault_flags |= FAULT_FLAG_REMOTE;
			if (locked)
				fault_flags |= FAULT_FLAG_ALLOW_RETRY |
					       FAULT_FLAG_KILLABLE;
			if (foll_flags & FOLL_TRIED)
				fault_flags |= FAULT_FLAG_TRIED;

			fault_ret =
				handle_mm_fault(vma, start, fault_flags, NULL);
			if (fault_ret & VM_FAULT_ERROR) {
				if (fault_ret & VM_FAULT_OOM) {
					ret = -ENOMEM;
					goto out;
				}
				if (fault_ret &
				    (VM_FAULT_HWPOISON |
				     VM_FAULT_HWPOISON_LARGE | VM_FAULT_SIGBUS |
				     VM_FAULT_SIGSEGV)) {
					ret = -EFAULT;
					goto out;
				}
				BUG();
			}

			if (fault_ret & VM_FAULT_RETRY) {
				if (locked)
					*locked = 0;
				/* -EBUSY case: ret = 0 and exit */
				goto out;
			}

			if ((fault_ret & VM_FAULT_WRITE) &&
			    !(vma->vm_flags & VM_WRITE))
				foll_flags |= FOLL_COW;

			goto retry;
		} else if (PTR_ERR(page) == -EEXIST) {
			if (pages) {
				ret = PTR_ERR(page);
				goto out;
			}

			goto next_page;
		} else if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto out;
		}
		if (pages) {
			pages[i] = page;
			ctx.page_mask = 0;
		}
next_page:
		if (vmas) {
			vmas[i] = vma;
			ctx.page_mask = 0;
		}
		page_increm = 1 + (~(start >> PAGE_SHIFT) & ctx.page_mask);
		if (page_increm > nr_pages)
			page_increm = nr_pages;
		i += page_increm;
		start += page_increm * PAGE_SIZE;
		nr_pages -= page_increm;
	} while (nr_pages);
out:
	return i ? i : ret;
}

long get_user_pages_remote(struct mm_struct *mm, unsigned long start,
			   unsigned long nr_pages, unsigned int gup_flags,
			   struct page **pages, struct vm_area_struct **vmas,
			   int *locked)
{
	if (pages)
		gup_flags |= FOLL_GET;
	return __get_user_pages(mm, start, nr_pages,
				gup_flags | FOLL_TOUCH | FOLL_REMOTE, pages,
				vmas, locked);
}
