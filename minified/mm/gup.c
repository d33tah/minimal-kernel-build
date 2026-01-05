#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/spinlock.h>

#include <linux/mm.h>
#include <linux/memremap.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
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

static int follow_pfn_pte(struct vm_area_struct *vma, unsigned long address,
			  pte_t *pte, unsigned int flags)
{
	if (flags & FOLL_TOUCH) {
		pte_t entry = *pte;

		if (flags & FOLL_WRITE)
			entry = pte_mkdirty(entry);
		entry = pte_mkyoung(entry);

		if (!pte_same(*pte, entry))
			set_pte_at(vma->vm_mm, address, pte, entry);
		/* update_mmu_cache - empty stub on x86 */
	}

	return -EEXIST;
}

/* can_follow_write_pte inlined into follow_page_pte */

static struct page *follow_page_pte(struct vm_area_struct *vma,
				    unsigned long address, pmd_t *pmd,
				    unsigned int flags,
				    struct dev_pagemap **pgmap)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;
	spinlock_t *ptl;
	pte_t *ptep, pte;
	int ret;

	/* FOLL_PIN|FOLL_GET check removed - FOLL_PIN never set */
retry:
	if (unlikely(pmd_bad(*pmd)))
		return no_page_table(vma, flags);

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	pte = *ptep;
	if (!pte_present(pte))
		goto no_page;
	/* pte_protnone always returns 0, FOLL_NUMA check removed */
	/* Inlined can_follow_write_pte */
	if ((flags & FOLL_WRITE) &&
	    !(pte_write(pte) ||
	      ((flags & FOLL_FORCE) && (flags & FOLL_COW) && pte_dirty(pte)))) {
		pte_unmap_unlock(ptep, ptl);
		return NULL;
	}

	page = vm_normal_page(vma, address, pte);
	/* pte_devmap always returns 0, devmap branch removed */
	if (unlikely(!page)) {
		/* FOLL_DUMP check removed - never set */
		if (is_zero_pfn(pte_pfn(pte))) {
			page = pte_page(pte);
		} else {
			ret = follow_pfn_pte(vma, address, ptep, flags);
			page = ERR_PTR(ret);
			goto out;
		}
	}

	/* gup_must_unshare always returns false - FOLL_PIN never set */

	if (unlikely(!try_grab_page(page, flags))) {
		page = ERR_PTR(-ENOMEM);
		goto out;
	}

	/* arch_make_page_accessible always returns 0, FOLL_PIN dead code removed */
	if (flags & FOLL_TOUCH) {
		if ((flags & FOLL_WRITE) && !pte_dirty(pte) && !PageDirty(page))
			set_page_dirty(page);

		mark_page_accessed(page);
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

static struct page *follow_pmd_mask(struct vm_area_struct *vma,
				    unsigned long address, pud_t *pudp,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	pmd_t *pmd, pmdval;

	pmd = pmd_offset(pudp, address);
	pmdval = READ_ONCE(*pmd);
	if (pmd_none(pmdval))
		return no_page_table(vma, flags);
	/* pmd_devmap/pmd_trans_huge always return 0 */
	if (!pmd_present(pmdval))
		return no_page_table(vma, flags);
	return follow_page_pte(vma, address, pmd, flags, &ctx->pgmap);
}

/* pud_none/pud_bad/p4d_none/p4d_bad always return 0 - folded paging */
static struct page *follow_pud_mask(struct vm_area_struct *vma,
				    unsigned long address, p4d_t *p4dp,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	pud_t *pud = pud_offset(p4dp, address);
	return follow_pmd_mask(vma, address, pud, flags, ctx);
}

static struct page *follow_p4d_mask(struct vm_area_struct *vma,
				    unsigned long address, pgd_t *pgdp,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	p4d_t *p4d = p4d_offset(pgdp, address);
	BUILD_BUG_ON(p4d_huge(*p4d));
	return follow_pud_mask(vma, address, p4d, flags, ctx);
}

static struct page *follow_page_mask(struct vm_area_struct *vma,
				     unsigned long address, unsigned int flags,
				     struct follow_page_context *ctx)
{
	pgd_t *pgd;
	struct page *page;
	struct mm_struct *mm = vma->vm_mm;

	ctx->page_mask = 0;

	page = follow_huge_addr(mm, address, flags & FOLL_WRITE);
	if (!IS_ERR(page)) {
		/* WARN removed - FOLL_PIN never set */
		return page;
	}

	/* pgd_none/pgd_bad always return 0 - folded paging */
	pgd = pgd_offset(mm, address);
	return follow_p4d_mask(vma, address, pgd, flags, ctx);
}

/* get_gate_page removed - in_gate_area always returns 0 */

static int faultin_page(struct vm_area_struct *vma, unsigned long address,
			unsigned int *flags, int *locked)
{
	unsigned int fault_flags = 0;
	vm_fault_t ret;
	/* FOLL_NOFAULT, FOLL_NOWAIT, FAULT_FLAG_UNSHARE checks removed - never set */
	if (*flags & FOLL_WRITE)
		fault_flags |= FAULT_FLAG_WRITE;
	if (*flags & FOLL_REMOTE)
		fault_flags |= FAULT_FLAG_REMOTE;
	if (locked)
		fault_flags |= FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;
	if (*flags & FOLL_TRIED)
		fault_flags |= FAULT_FLAG_TRIED;

	ret = handle_mm_fault(vma, address, fault_flags, NULL);
	if (ret & VM_FAULT_ERROR) {
		int err = vm_fault_to_errno(ret, *flags);

		if (err)
			return err;
		BUG();
	}

	if (ret & VM_FAULT_RETRY) {
		/* FAULT_FLAG_RETRY_NOWAIT never set */
		if (locked)
			*locked = 0;
		return -EBUSY;
	}

	if ((ret & VM_FAULT_WRITE) && !(vma->vm_flags & VM_WRITE))
		*flags |= FOLL_COW;
	return 0;
}

static int check_vma_flags(struct vm_area_struct *vma, unsigned long gup_flags)
{
	vm_flags_t vm_flags = vma->vm_flags;
	int write = (gup_flags & FOLL_WRITE);
	int foreign = (gup_flags & FOLL_REMOTE);

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
			ret = faultin_page(vma, start, &foll_flags, locked);
			switch (ret) {
			case 0:
				goto retry;
			case -EBUSY:
				ret = 0;
				fallthrough;
			case -EFAULT:
			case -ENOMEM:
			case -EHWPOISON:
				goto out;
			}
			BUG();
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

	if (vma->vm_flags & VM_LOCKONFAULT)
		return nr_pages;

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

size_t fault_in_readable(const char __user *uaddr, size_t size)
{
	const char __user *start = uaddr, *end;
	volatile char c;

	if (unlikely(size == 0))
		return 0;
	if (!user_read_access_begin(uaddr, size))
		return size;
	if (!PAGE_ALIGNED(uaddr)) {
		unsafe_get_user(c, uaddr, out);
		uaddr = (const char __user *)PAGE_ALIGN((unsigned long)uaddr);
	}
	end = (const char __user *)PAGE_ALIGN((unsigned long)start + size);
	if (unlikely(end < start))
		end = NULL;
	while (uaddr != end) {
		unsafe_get_user(c, uaddr, out);
		uaddr += PAGE_SIZE;
	}

out:
	user_read_access_end();
	(void)c;
	if (size > uaddr - start)
		return size - (uaddr - start);
	return 0;
}

/* __gup_longterm_locked removed - FOLL_LONGTERM never set, function never called */

/* is_valid_gup_flags inlined into get_user_pages_remote */

/* __get_user_pages_remote simplified - FOLL_LONGTERM branch removed (never set) */
static long __get_user_pages_remote(struct mm_struct *mm, unsigned long start,
				    unsigned long nr_pages,
				    unsigned int gup_flags, struct page **pages,
				    struct vm_area_struct **vmas, int *locked)
{
	return __get_user_pages_locked(mm, start, nr_pages, pages, vmas, locked,
				       gup_flags | FOLL_TOUCH | FOLL_REMOTE);
}

long get_user_pages_remote(struct mm_struct *mm, unsigned long start,
			   unsigned long nr_pages, unsigned int gup_flags,
			   struct page **pages, struct vm_area_struct **vmas,
			   int *locked)
{
	/* FOLL_PIN and FOLL_LONGTERM checks removed - never set */
	return __get_user_pages_remote(mm, start, nr_pages, gup_flags, pages,
				       vmas, locked);
}

/* get_user_pages_unlocked, get_user_pages_fast_only, get_user_pages_fast,
   pin_user_pages_fast, pin_user_pages_fast_only, pin_user_pages_remote,
   pin_user_pages, pin_user_pages_unlocked, get_user_pages removed - unused */
