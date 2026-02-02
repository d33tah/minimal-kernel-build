#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/spinlock.h>

#include <linux/mm.h>
#include <linux/memremap.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
/* swapops.h removed - was empty */
#include <linux/sched/signal.h>
#include <linux/rwsem.h>
#include <linux/hugetlb.h>
/* linux/migrate.h removed - empty header */
#include <linux/mm_inline.h>
#include <linux/sched/mm.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

#include "internal.h"

struct follow_page_context {
	struct dev_pagemap *pgmap;
	unsigned int page_mask;
};

/* try_get_folio removed - never called */
bool __must_check try_grab_page(struct page *page, unsigned int flags)
{
	struct folio *folio = page_folio(page);
	/* FOLL_PIN never set, simplified */
	if (WARN_ON_ONCE(folio_ref_count(folio) <= 0))
		return false;

	if (flags & FOLL_GET)
		folio_ref_inc(folio);
	/* FOLL_PIN branch removed - never set */

	return true;
}

/* mm_set_has_pinned_flag inlined into __get_user_pages_locked */

static struct page *no_page_table(struct vm_area_struct *vma,
				  unsigned int flags)
{
	/* FOLL_DUMP check removed - never set */
	return NULL;
}

/* follow_pfn_pte, follow_page_pte, follow_pmd_mask, follow_pud_mask, follow_p4d_mask inlined */

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

	/* Inlined follow_pmd_mask */
	pmd = pmd_offset(pud, address);
	pmdval = READ_ONCE(*pmd);
	if (pmd_none(pmdval))
		return no_page_table(vma, flags);
	if (!pmd_present(pmdval))
		return no_page_table(vma, flags);

	/* Inlined follow_page_pte */
	if (unlikely(pmd_bad(*pmd)))
		return no_page_table(vma, flags);

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	pte = *ptep;
	if (!pte_present(pte))
		goto no_page;

	/* Inlined can_follow_write_pte */
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
			/* follow_pfn_pte inlined */
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
		/* folio_mark_accessed removed - was empty stub */
	}
out:
	pte_unmap_unlock(ptep, ptl);
	return page;
no_page:
	pte_unmap_unlock(ptep, ptl);
	if (!pte_none(pte))
		return NULL;
	return no_page_table(vma, flags);
}

/* get_gate_page removed - in_gate_area always returns 0 */
/* faultin_page inlined into __get_user_pages */

static int check_vma_flags(struct vm_area_struct *vma, unsigned long gup_flags)
{
	vm_flags_t vm_flags = vma->vm_flags;
	int write = (gup_flags & FOLL_WRITE);
	/* foreign removed - never used after FOLL_REMOTE simplification */

	if (vm_flags & (VM_IO | VM_PFNMAP))
		return -EFAULT;

	if (gup_flags & FOLL_ANON && !vma_is_anonymous(vma))
		return -EFAULT;

	/* vma_is_fsdax always returns false */

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

	/* arch_vma_access_permitted always returns true (OSPKE disabled) */
	return 0;
}

static long __get_user_pages(struct mm_struct *mm, unsigned long start,
			     unsigned long nr_pages, unsigned int gup_flags,
			     struct page **pages, struct vm_area_struct **vmas,
			     int *locked)
{
	long ret = 0, i = 0;
	struct vm_area_struct *vma = NULL;
	struct follow_page_context ctx = { NULL };

	if (!nr_pages)
		return 0;

	start = untagged_addr(start);
	/* FOLL_NUMA setting removed - never tested */

	do {
		struct page *page;
		unsigned int foll_flags = gup_flags;
		unsigned int page_increm;

		if (!vma || start >= vma->vm_end) {
			vma = find_extend_vma(mm, start);
			/* in_gate_area always returns 0, gate page code removed */
			if (!vma) {
				ret = -EFAULT;
				goto out;
			}
			ret = check_vma_flags(vma, gup_flags);
			if (ret)
				goto out;
			/* hugetlb page handling removed - is_vm_hugetlb_page always returns false */
		}
retry:

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
		cond_resched();

		page = follow_page_mask(vma, start, foll_flags, &ctx);
		/* -EMLINK check removed - gup_must_unshare always false */
		if (!page) {
			/* faultin_page inlined */
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
				if (fault_ret & (VM_FAULT_HWPOISON |
						 VM_FAULT_HWPOISON_LARGE)) {
					ret = (foll_flags & FOLL_HWPOISON) ?
						      -EHWPOISON :
						      -EFAULT;
					goto out;
				}
				if (fault_ret &
				    (VM_FAULT_SIGBUS | VM_FAULT_SIGSEGV)) {
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
			/* flush_anon_page and flush_dcache_page - empty stubs on x86 */
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
	if (ctx.pgmap)
		put_dev_pagemap(ctx.pgmap);
	return i ? i : ret;
}

static __always_inline long
__get_user_pages_locked(struct mm_struct *mm, unsigned long start,
			unsigned long nr_pages, struct page **pages,
			struct vm_area_struct **vmas, int *locked,
			unsigned int flags)
{
	long ret, pages_done;
	bool lock_dropped;

	if (locked) {
		BUG_ON(vmas);

		BUG_ON(*locked != 1);
	}

	/* FOLL_PIN MMF_HAS_PINNED check removed - FOLL_PIN never set */

	if (pages)
		flags |= FOLL_GET;

	pages_done = 0;
	lock_dropped = false;
	for (;;) {
		ret = __get_user_pages(mm, start, nr_pages, flags, pages, vmas,
				       locked);
		if (!locked)

			return ret;

		if (!*locked) {
			BUG_ON(ret < 0);
			BUG_ON(ret >= nr_pages);
		}

		if (ret > 0) {
			nr_pages -= ret;
			pages_done += ret;
			if (!nr_pages)
				break;
		}
		if (*locked) {
			if (!pages_done)
				pages_done = ret;
			break;
		}

		if (likely(pages))
			pages += ret;
		start += ret << PAGE_SHIFT;
		lock_dropped = true;

retry:

		if (fatal_signal_pending(current)) {
			if (!pages_done)
				pages_done = -EINTR;
			break;
		}

		ret = mmap_read_lock_killable(mm);
		if (ret) {
			BUG_ON(ret > 0);
			if (!pages_done)
				pages_done = ret;
			break;
		}

		*locked = 1;
		ret = __get_user_pages(mm, start, 1, flags | FOLL_TRIED, pages,
				       NULL, locked);
		if (!*locked) {
			BUG_ON(ret != 0);
			goto retry;
		}
		if (ret != 1) {
			BUG_ON(ret > 1);
			if (!pages_done)
				pages_done = ret;
			break;
		}
		nr_pages--;
		pages_done++;
		if (!nr_pages)
			break;
		if (likely(pages))
			pages++;
		start += PAGE_SIZE;
	}
	if (lock_dropped && *locked) {
		mmap_read_unlock(mm);
		*locked = 0;
	}
	return pages_done;
}

long populate_vma_page_range(struct vm_area_struct *vma, unsigned long start,
			     unsigned long end, int *locked)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long nr_pages = (end - start) / PAGE_SIZE;
	int gup_flags;
	long ret;

	mmap_assert_locked(mm);
	/* VM_LOCKONFAULT check removed - never set */

	gup_flags = FOLL_TOUCH;

	if ((vma->vm_flags & (VM_WRITE | VM_SHARED)) == VM_WRITE)
		gup_flags |= FOLL_WRITE;

	if (vma_is_accessible(vma))
		gup_flags |= FOLL_FORCE;

	ret = __get_user_pages(mm, start, nr_pages, gup_flags, NULL, NULL,
			       locked);
	lru_add_drain();
	return ret;
}

int __mm_populate(unsigned long start, unsigned long len, int ignore_errors)
{
	struct mm_struct *mm = current->mm;
	unsigned long end, nstart, nend;
	struct vm_area_struct *vma = NULL;
	int locked = 0;
	long ret = 0;

	end = start + len;

	for (nstart = start; nstart < end; nstart = nend) {
		if (!locked) {
			locked = 1;
			mmap_read_lock(mm);
			vma = find_vma(mm, nstart);
		} else if (nstart >= vma->vm_end)
			vma = vma->vm_next;
		if (!vma || vma->vm_start >= end)
			break;

		nend = min(end, vma->vm_end);
		if (vma->vm_flags & (VM_IO | VM_PFNMAP))
			continue;
		if (nstart < vma->vm_start)
			nstart = vma->vm_start;

		ret = populate_vma_page_range(vma, nstart, nend, &locked);
		if (ret < 0) {
			if (ignore_errors) {
				ret = 0;
				continue;
			}
			break;
		}
		nend = nstart + ret * PAGE_SIZE;
		ret = 0;
	}
	if (locked)
		mmap_read_unlock(mm);
	return ret;
}

/* fault_in_readable removed - never called */

/* __gup_longterm_locked removed - FOLL_LONGTERM never set, function never called */

/* is_valid_gup_flags, __get_user_pages_remote inlined */

long get_user_pages_remote(struct mm_struct *mm, unsigned long start,
			   unsigned long nr_pages, unsigned int gup_flags,
			   struct page **pages, struct vm_area_struct **vmas,
			   int *locked)
{
	/* FOLL_PIN and FOLL_LONGTERM checks removed - never set */
	return __get_user_pages_locked(mm, start, nr_pages, pages, vmas, locked,
				       gup_flags | FOLL_TOUCH | FOLL_REMOTE);
}

/* get_user_pages_unlocked, get_user_pages_fast_only, get_user_pages_fast,
   pin_user_pages_fast, pin_user_pages_fast_only, pin_user_pages_remote,
   pin_user_pages, pin_user_pages_unlocked, get_user_pages removed - unused */
