 
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
#include <linux/secretmem.h>

#include <linux/sched/signal.h>
#include <linux/rwsem.h>
#include <linux/hugetlb.h>
#include <linux/migrate.h>
#include <linux/mm_inline.h>
#include <linux/sched/mm.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

#include "internal.h"

struct follow_page_context {
	struct dev_pagemap *pgmap;
	unsigned int page_mask;
};

static inline void sanity_check_pinned_pages(struct page **pages,
					     unsigned long npages)
{
	if (!IS_ENABLED(CONFIG_DEBUG_VM))
		return;

	
	for (; npages; npages--, pages++) {
		struct page *page = *pages;
		struct folio *folio = page_folio(page);

		if (!folio_test_anon(folio))
			continue;
		if (!folio_test_large(folio) || folio_test_hugetlb(folio))
			VM_BUG_ON_PAGE(!PageAnonExclusive(&folio->page), page);
		else
			
			VM_BUG_ON_PAGE(!PageAnonExclusive(&folio->page) &&
				       !PageAnonExclusive(page), page);
	}
}

static inline struct folio *try_get_folio(struct page *page, int refs)
{
	struct folio *folio;

retry:
	folio = page_folio(page);
	if (WARN_ON_ONCE(folio_ref_count(folio) < 0))
		return NULL;
	if (unlikely(!folio_ref_try_add_rcu(folio, refs)))
		return NULL;

	
	if (unlikely(page_folio(page) != folio)) {
		folio_put_refs(folio, refs);
		goto retry;
	}

	return folio;
}

struct folio *try_grab_folio(struct page *page, int refs, unsigned int flags)
{
	if (flags & FOLL_GET)
		return try_get_folio(page, refs);
	else if (flags & FOLL_PIN) {
		struct folio *folio;

		
		if (unlikely((flags & FOLL_LONGTERM) &&
			     !is_pinnable_page(page)))
			return NULL;

		
		folio = try_get_folio(page, refs);
		if (!folio)
			return NULL;

		
		if (folio_test_large(folio))
			atomic_add(refs, folio_pincount_ptr(folio));
		else
			folio_ref_add(folio,
					refs * (GUP_PIN_COUNTING_BIAS - 1));
		node_stat_mod_folio(folio, NR_FOLL_PIN_ACQUIRED, refs);

		return folio;
	}

	WARN_ON_ONCE(1);
	return NULL;
}

static void gup_put_folio(struct folio *folio, int refs, unsigned int flags)
{
	if (flags & FOLL_PIN) {
		node_stat_mod_folio(folio, NR_FOLL_PIN_RELEASED, refs);
		if (folio_test_large(folio))
			atomic_sub(refs, folio_pincount_ptr(folio));
		else
			refs *= GUP_PIN_COUNTING_BIAS;
	}

	folio_put_refs(folio, refs);
}

bool __must_check try_grab_page(struct page *page, unsigned int flags)
{
	struct folio *folio = page_folio(page);

	WARN_ON_ONCE((flags & (FOLL_GET | FOLL_PIN)) == (FOLL_GET | FOLL_PIN));
	if (WARN_ON_ONCE(folio_ref_count(folio) <= 0))
		return false;

	if (flags & FOLL_GET)
		folio_ref_inc(folio);
	else if (flags & FOLL_PIN) {
		
		if (folio_test_large(folio)) {
			folio_ref_add(folio, 1);
			atomic_add(1, folio_pincount_ptr(folio));
		} else {
			folio_ref_add(folio, GUP_PIN_COUNTING_BIAS);
		}

		node_stat_mod_folio(folio, NR_FOLL_PIN_ACQUIRED, 1);
	}

	return true;
}

void unpin_user_page(struct page *page)
{
	sanity_check_pinned_pages(&page, 1);
	gup_put_folio(page_folio(page), 1, FOLL_PIN);
}

static inline struct folio *gup_folio_range_next(struct page *start,
		unsigned long npages, unsigned long i, unsigned int *ntails)
{
	struct page *next = nth_page(start, i);
	struct folio *folio = page_folio(next);
	unsigned int nr = 1;

	if (folio_test_large(folio))
		nr = min_t(unsigned int, npages - i,
			   folio_nr_pages(folio) - folio_page_idx(folio, next));

	*ntails = nr;
	return folio;
}

static inline struct folio *gup_folio_next(struct page **list,
		unsigned long npages, unsigned long i, unsigned int *ntails)
{
	struct folio *folio = page_folio(list[i]);
	unsigned int nr;

	for (nr = i + 1; nr < npages; nr++) {
		if (page_folio(list[nr]) != folio)
			break;
	}

	*ntails = nr - i;
	return folio;
}

/* Stubbed - not used externally */
void unpin_user_pages_dirty_lock(struct page **pages, unsigned long npages,
				 bool make_dirty)
{
}

/* Stubbed - not used externally */
void unpin_user_page_range_dirty_lock(struct page *page, unsigned long npages,
				      bool make_dirty)
{
}

void unpin_user_pages(struct page **pages, unsigned long npages)
{
	unsigned long i;
	struct folio *folio;
	unsigned int nr;

	
	if (WARN_ON(IS_ERR_VALUE(npages)))
		return;

	sanity_check_pinned_pages(pages, npages);
	for (i = 0; i < npages; i += nr) {
		folio = gup_folio_next(pages, npages, i, &nr);
		gup_put_folio(folio, nr, FOLL_PIN);
	}
}

static inline void mm_set_has_pinned_flag(unsigned long *mm_flags)
{
	if (!test_bit(MMF_HAS_PINNED, mm_flags))
		set_bit(MMF_HAS_PINNED, mm_flags);
}

static struct page *no_page_table(struct vm_area_struct *vma,
		unsigned int flags)
{
	
	if ((flags & FOLL_DUMP) &&
			(vma_is_anonymous(vma) || !vma->vm_ops->fault))
		return ERR_PTR(-EFAULT);
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
			update_mmu_cache(vma, address, pte);
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
		unsigned long address, pmd_t *pmd, unsigned int flags,
		struct dev_pagemap **pgmap)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;
	spinlock_t *ptl;
	pte_t *ptep, pte;
	int ret;

	
	if (WARN_ON_ONCE((flags & (FOLL_PIN | FOLL_GET)) ==
			 (FOLL_PIN | FOLL_GET)))
		return ERR_PTR(-EINVAL);
retry:
	if (unlikely(pmd_bad(*pmd)))
		return no_page_table(vma, flags);

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	pte = *ptep;
	if (!pte_present(pte)) {
		swp_entry_t entry;
		
		if (likely(!(flags & FOLL_MIGRATION)))
			goto no_page;
		if (pte_none(pte))
			goto no_page;
		entry = pte_to_swp_entry(pte);
		if (!is_migration_entry(entry))
			goto no_page;
		pte_unmap_unlock(ptep, ptl);
		migration_entry_wait(mm, pmd, address);
		goto retry;
	}
	if ((flags & FOLL_NUMA) && pte_protnone(pte))
		goto no_page;
	if ((flags & FOLL_WRITE) && !can_follow_write_pte(pte, flags)) {
		pte_unmap_unlock(ptep, ptl);
		return NULL;
	}

	page = vm_normal_page(vma, address, pte);
	if (!page && pte_devmap(pte) && (flags & (FOLL_GET | FOLL_PIN))) {
		
		*pgmap = get_dev_pagemap(pte_pfn(pte), *pgmap);
		if (*pgmap)
			page = pte_page(pte);
		else
			goto no_page;
	} else if (unlikely(!page)) {
		if (flags & FOLL_DUMP) {
			
			page = ERR_PTR(-EFAULT);
			goto out;
		}

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

	VM_BUG_ON_PAGE((flags & FOLL_PIN) && PageAnon(page) &&
		       !PageAnonExclusive(page), page);

	
	if (unlikely(!try_grab_page(page, flags))) {
		page = ERR_PTR(-ENOMEM);
		goto out;
	}
	
	if (flags & FOLL_PIN) {
		ret = arch_make_page_accessible(page);
		if (ret) {
			unpin_user_page(page);
			page = ERR_PTR(ret);
			goto out;
		}
	}
	if (flags & FOLL_TOUCH) {
		if ((flags & FOLL_WRITE) &&
		    !pte_dirty(pte) && !PageDirty(page))
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
	spinlock_t *ptl;
	struct page *page;
	struct mm_struct *mm = vma->vm_mm;

	pmd = pmd_offset(pudp, address);
	
	pmdval = READ_ONCE(*pmd);
	if (pmd_none(pmdval))
		return no_page_table(vma, flags);
	if (pmd_huge(pmdval) && is_vm_hugetlb_page(vma)) {
		page = follow_huge_pmd(mm, address, pmd, flags);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
	if (is_hugepd(__hugepd(pmd_val(pmdval)))) {
		page = follow_huge_pd(vma, address,
				      __hugepd(pmd_val(pmdval)), flags,
				      PMD_SHIFT);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
retry:
	if (!pmd_present(pmdval)) {
		
		VM_BUG_ON(!thp_migration_supported() ||
				  !is_pmd_migration_entry(pmdval));

		if (likely(!(flags & FOLL_MIGRATION)))
			return no_page_table(vma, flags);

		pmd_migration_entry_wait(mm, pmd);
		pmdval = READ_ONCE(*pmd);
		
		if (pmd_none(pmdval))
			return no_page_table(vma, flags);
		goto retry;
	}
	if (pmd_devmap(pmdval)) {
		ptl = pmd_lock(mm, pmd);
		page = follow_devmap_pmd(vma, address, pmd, flags, &ctx->pgmap);
		spin_unlock(ptl);
		if (page)
			return page;
	}
	if (likely(!pmd_trans_huge(pmdval)))
		return follow_page_pte(vma, address, pmd, flags, &ctx->pgmap);

	if ((flags & FOLL_NUMA) && pmd_protnone(pmdval))
		return no_page_table(vma, flags);

retry_locked:
	ptl = pmd_lock(mm, pmd);
	if (unlikely(pmd_none(*pmd))) {
		spin_unlock(ptl);
		return no_page_table(vma, flags);
	}
	if (unlikely(!pmd_present(*pmd))) {
		spin_unlock(ptl);
		if (likely(!(flags & FOLL_MIGRATION)))
			return no_page_table(vma, flags);
		pmd_migration_entry_wait(mm, pmd);
		goto retry_locked;
	}
	if (unlikely(!pmd_trans_huge(*pmd))) {
		spin_unlock(ptl);
		return follow_page_pte(vma, address, pmd, flags, &ctx->pgmap);
	}
	if (flags & FOLL_SPLIT_PMD) {
		int ret;
		page = pmd_page(*pmd);
		if (is_huge_zero_page(page)) {
			spin_unlock(ptl);
			ret = 0;
			split_huge_pmd(vma, pmd, address);
			if (pmd_trans_unstable(pmd))
				ret = -EBUSY;
		} else {
			spin_unlock(ptl);
			split_huge_pmd(vma, pmd, address);
			ret = pte_alloc(mm, pmd) ? -ENOMEM : 0;
		}

		return ret ? ERR_PTR(ret) :
			follow_page_pte(vma, address, pmd, flags, &ctx->pgmap);
	}
	page = follow_trans_huge_pmd(vma, address, pmd, flags);
	spin_unlock(ptl);
	ctx->page_mask = HPAGE_PMD_NR - 1;
	return page;
}

static struct page *follow_pud_mask(struct vm_area_struct *vma,
				    unsigned long address, p4d_t *p4dp,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	pud_t *pud;
	spinlock_t *ptl;
	struct page *page;
	struct mm_struct *mm = vma->vm_mm;

	pud = pud_offset(p4dp, address);
	if (pud_none(*pud))
		return no_page_table(vma, flags);
	if (pud_huge(*pud) && is_vm_hugetlb_page(vma)) {
		page = follow_huge_pud(mm, address, pud, flags);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
	if (is_hugepd(__hugepd(pud_val(*pud)))) {
		page = follow_huge_pd(vma, address,
				      __hugepd(pud_val(*pud)), flags,
				      PUD_SHIFT);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
	if (pud_devmap(*pud)) {
		ptl = pud_lock(mm, pud);
		page = follow_devmap_pud(vma, address, pud, flags, &ctx->pgmap);
		spin_unlock(ptl);
		if (page)
			return page;
	}
	if (unlikely(pud_bad(*pud)))
		return no_page_table(vma, flags);

	return follow_pmd_mask(vma, address, pud, flags, ctx);
}

static struct page *follow_p4d_mask(struct vm_area_struct *vma,
				    unsigned long address, pgd_t *pgdp,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	p4d_t *p4d;
	struct page *page;

	p4d = p4d_offset(pgdp, address);
	if (p4d_none(*p4d))
		return no_page_table(vma, flags);
	BUILD_BUG_ON(p4d_huge(*p4d));
	if (unlikely(p4d_bad(*p4d)))
		return no_page_table(vma, flags);

	if (is_hugepd(__hugepd(p4d_val(*p4d)))) {
		page = follow_huge_pd(vma, address,
				      __hugepd(p4d_val(*p4d)), flags,
				      P4D_SHIFT);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
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
		WARN_ON_ONCE(flags & (FOLL_GET | FOLL_PIN));
		return page;
	}

	pgd = pgd_offset(mm, address);

	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		return no_page_table(vma, flags);

	if (pgd_huge(*pgd)) {
		page = follow_huge_pgd(mm, address, pgd, flags);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
	if (is_hugepd(__hugepd(pgd_val(*pgd)))) {
		page = follow_huge_pd(vma, address,
				      __hugepd(pgd_val(*pgd)), flags,
				      PGDIR_SHIFT);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}

	return follow_p4d_mask(vma, address, pgd, flags, ctx);
}

struct page *follow_page(struct vm_area_struct *vma, unsigned long address,
			 unsigned int foll_flags)
{
	struct follow_page_context ctx = { NULL };
	struct page *page;

	if (vma_is_secretmem(vma))
		return NULL;

	if (foll_flags & FOLL_PIN)
		return NULL;

	page = follow_page_mask(vma, address, foll_flags, &ctx);
	if (ctx.pgmap)
		put_dev_pagemap(ctx.pgmap);
	return page;
}

static int get_gate_page(struct mm_struct *mm, unsigned long address,
		unsigned int gup_flags, struct vm_area_struct **vma,
		struct page **page)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int ret = -EFAULT;

	
	if (gup_flags & FOLL_WRITE)
		return -EFAULT;
	if (address > TASK_SIZE)
		pgd = pgd_offset_k(address);
	else
		pgd = pgd_offset_gate(mm, address);
	if (pgd_none(*pgd))
		return -EFAULT;
	p4d = p4d_offset(pgd, address);
	if (p4d_none(*p4d))
		return -EFAULT;
	pud = pud_offset(p4d, address);
	if (pud_none(*pud))
		return -EFAULT;
	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd))
		return -EFAULT;
	VM_BUG_ON(pmd_trans_huge(*pmd));
	pte = pte_offset_map(pmd, address);
	if (pte_none(*pte))
		goto unmap;
	*vma = get_gate_vma(mm);
	if (!page)
		goto out;
	*page = vm_normal_page(*vma, address, *pte);
	if (!*page) {
		if ((gup_flags & FOLL_DUMP) || !is_zero_pfn(pte_pfn(*pte)))
			goto unmap;
		*page = pte_page(*pte);
	}
	if (unlikely(!try_grab_page(*page, gup_flags))) {
		ret = -ENOMEM;
		goto unmap;
	}
out:
	ret = 0;
unmap:
	pte_unmap(pte);
	return ret;
}

static int faultin_page(struct vm_area_struct *vma,
		unsigned long address, unsigned int *flags, bool unshare,
		int *locked)
{
	unsigned int fault_flags = 0;
	vm_fault_t ret;

	if (*flags & FOLL_NOFAULT)
		return -EFAULT;
	if (*flags & FOLL_WRITE)
		fault_flags |= FAULT_FLAG_WRITE;
	if (*flags & FOLL_REMOTE)
		fault_flags |= FAULT_FLAG_REMOTE;
	if (locked)
		fault_flags |= FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;
	if (*flags & FOLL_NOWAIT)
		fault_flags |= FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_RETRY_NOWAIT;
	if (*flags & FOLL_TRIED) {
		
		fault_flags |= FAULT_FLAG_TRIED;
	}
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

	if (ret & VM_FAULT_RETRY) {
		if (locked && !(fault_flags & FAULT_FLAG_RETRY_NOWAIT))
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

	if ((gup_flags & FOLL_LONGTERM) && vma_is_fsdax(vma))
		return -EOPNOTSUPP;

	if (vma_is_secretmem(vma))
		return -EFAULT;

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
	
	if (!arch_vma_access_permitted(vma, write, false, foreign))
		return -EFAULT;
	return 0;
}

static long __get_user_pages(struct mm_struct *mm,
		unsigned long start, unsigned long nr_pages,
		unsigned int gup_flags, struct page **pages,
		struct vm_area_struct **vmas, int *locked)
{
	long ret = 0, i = 0;
	struct vm_area_struct *vma = NULL;
	struct follow_page_context ctx = { NULL };

	if (!nr_pages)
		return 0;

	start = untagged_addr(start);

	VM_BUG_ON(!!pages != !!(gup_flags & (FOLL_GET | FOLL_PIN)));

	
	if (!(gup_flags & FOLL_FORCE))
		gup_flags |= FOLL_NUMA;

	do {
		struct page *page;
		unsigned int foll_flags = gup_flags;
		unsigned int page_increm;

		
		if (!vma || start >= vma->vm_end) {
			vma = find_extend_vma(mm, start);
			if (!vma && in_gate_area(mm, start)) {
				ret = get_gate_page(mm, start & PAGE_MASK,
						gup_flags, &vma,
						pages ? &pages[i] : NULL);
				if (ret)
					goto out;
				ctx.page_mask = 0;
				goto next_page;
			}

			if (!vma) {
				ret = -EFAULT;
				goto out;
			}
			ret = check_vma_flags(vma, gup_flags);
			if (ret)
				goto out;

			if (is_vm_hugetlb_page(vma)) {
				i = follow_hugetlb_page(mm, vma, pages, vmas,
						&start, &nr_pages, i,
						gup_flags, locked);
				if (locked && *locked == 0) {
					
					BUG_ON(gup_flags & FOLL_NOWAIT);
					goto out;
				}
				continue;
			}
		}
retry:
		
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
		cond_resched();

		page = follow_page_mask(vma, start, foll_flags, &ctx);
		if (!page || PTR_ERR(page) == -EMLINK) {
			ret = faultin_page(vma, start, &foll_flags,
					   PTR_ERR(page) == -EMLINK, locked);
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
			flush_anon_page(vma, page, start);
			flush_dcache_page(page);
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

/* Stubbed - not used externally */
int fixup_user_fault(struct mm_struct *mm,
		     unsigned long address, unsigned int fault_flags,
		     bool *unlocked)
{
	return -EFAULT;
}

static __always_inline long __get_user_pages_locked(struct mm_struct *mm,
						unsigned long start,
						unsigned long nr_pages,
						struct page **pages,
						struct vm_area_struct **vmas,
						int *locked,
						unsigned int flags)
{
	long ret, pages_done;
	bool lock_dropped;

	if (locked) {
		
		BUG_ON(vmas);
		
		BUG_ON(*locked != 1);
	}

	if (flags & FOLL_PIN)
		mm_set_has_pinned_flag(&mm->flags);

	
	if (pages && !(flags & FOLL_PIN))
		flags |= FOLL_GET;

	pages_done = 0;
	lock_dropped = false;
	for (;;) {
		ret = __get_user_pages(mm, start, nr_pages, flags, pages,
				       vmas, locked);
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
		ret = __get_user_pages(mm, start, 1, flags | FOLL_TRIED,
				       pages, NULL, locked);
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

long populate_vma_page_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end, int *locked)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long nr_pages = (end - start) / PAGE_SIZE;
	int gup_flags;
	long ret;

	VM_BUG_ON(!PAGE_ALIGNED(start));
	VM_BUG_ON(!PAGE_ALIGNED(end));
	VM_BUG_ON_VMA(start < vma->vm_start, vma);
	VM_BUG_ON_VMA(end   > vma->vm_end, vma);
	mmap_assert_locked(mm);

	
	if (vma->vm_flags & VM_LOCKONFAULT)
		return nr_pages;

	gup_flags = FOLL_TOUCH;
	
	if ((vma->vm_flags & (VM_WRITE | VM_SHARED)) == VM_WRITE)
		gup_flags |= FOLL_WRITE;

	
	if (vma_is_accessible(vma))
		gup_flags |= FOLL_FORCE;

	
	ret = __get_user_pages(mm, start, nr_pages, gup_flags,
				NULL, NULL, locked);
	lru_add_drain();
	return ret;
}

long faultin_vma_page_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end, bool write, int *locked)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long nr_pages = (end - start) / PAGE_SIZE;
	int gup_flags;
	long ret;

	VM_BUG_ON(!PAGE_ALIGNED(start));
	VM_BUG_ON(!PAGE_ALIGNED(end));
	VM_BUG_ON_VMA(start < vma->vm_start, vma);
	VM_BUG_ON_VMA(end > vma->vm_end, vma);
	mmap_assert_locked(mm);

	
	gup_flags = FOLL_TOUCH | FOLL_HWPOISON;
	if (write)
		gup_flags |= FOLL_WRITE;

	
	if (check_vma_flags(vma, gup_flags))
		return -EINVAL;

	ret = __get_user_pages(mm, start, nr_pages, gup_flags,
				NULL, NULL, locked);
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

size_t fault_in_writeable(char __user *uaddr, size_t size)
{
	char __user *start = uaddr, *end;

	if (unlikely(size == 0))
		return 0;
	if (!user_write_access_begin(uaddr, size))
		return size;
	if (!PAGE_ALIGNED(uaddr)) {
		unsafe_put_user(0, uaddr, out);
		uaddr = (char __user *)PAGE_ALIGN((unsigned long)uaddr);
	}
	end = (char __user *)PAGE_ALIGN((unsigned long)start + size);
	if (unlikely(end < start))
		end = NULL;
	while (uaddr != end) {
		unsafe_put_user(0, uaddr, out);
		uaddr += PAGE_SIZE;
	}

out:
	user_write_access_end();
	if (size > uaddr - start)
		return size - (uaddr - start);
	return 0;
}

/* Stubbed - not used externally */
size_t fault_in_subpage_writeable(char __user *uaddr, size_t size)
{
	return size;
}

/* Stubbed - not used externally */
size_t fault_in_safe_writeable(const char __user *uaddr, size_t size)
{
	return size;
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

static long check_and_migrate_movable_pages(unsigned long nr_pages,
					    struct page **pages,
					    unsigned int gup_flags)
{
	return nr_pages;
}

static long __gup_longterm_locked(struct mm_struct *mm,
				  unsigned long start,
				  unsigned long nr_pages,
				  struct page **pages,
				  struct vm_area_struct **vmas,
				  unsigned int gup_flags)
{
	unsigned int flags;
	long rc;

	if (!(gup_flags & FOLL_LONGTERM))
		return __get_user_pages_locked(mm, start, nr_pages, pages, vmas,
					       NULL, gup_flags);
	flags = memalloc_pin_save();
	do {
		rc = __get_user_pages_locked(mm, start, nr_pages, pages, vmas,
					     NULL, gup_flags);
		if (rc <= 0)
			break;
		rc = check_and_migrate_movable_pages(rc, pages, gup_flags);
	} while (!rc);
	memalloc_pin_restore(flags);

	return rc;
}

static bool is_valid_gup_flags(unsigned int gup_flags)
{
	
	if (WARN_ON_ONCE(gup_flags & FOLL_PIN))
		return false;
	
	if (WARN_ON_ONCE(gup_flags & FOLL_LONGTERM))
		return false;

	return true;
}

static long __get_user_pages_remote(struct mm_struct *mm,
				    unsigned long start, unsigned long nr_pages,
				    unsigned int gup_flags, struct page **pages,
				    struct vm_area_struct **vmas, int *locked)
{
	
	if (gup_flags & FOLL_LONGTERM) {
		if (WARN_ON_ONCE(locked))
			return -EINVAL;
		
		return __gup_longterm_locked(mm, start, nr_pages, pages,
					     vmas, gup_flags | FOLL_TOUCH |
					     FOLL_REMOTE);
	}

	return __get_user_pages_locked(mm, start, nr_pages, pages, vmas,
				       locked,
				       gup_flags | FOLL_TOUCH | FOLL_REMOTE);
}

long get_user_pages_remote(struct mm_struct *mm,
		unsigned long start, unsigned long nr_pages,
		unsigned int gup_flags, struct page **pages,
		struct vm_area_struct **vmas, int *locked)
{
	if (!is_valid_gup_flags(gup_flags))
		return -EINVAL;

	return __get_user_pages_remote(mm, start, nr_pages, gup_flags,
				       pages, vmas, locked);
}

/* Stubbed - not used externally */
long get_user_pages(unsigned long start, unsigned long nr_pages,
		unsigned int gup_flags, struct page **pages,
		struct vm_area_struct **vmas)
{
	return -EINVAL;
}

/* Stubbed - not used externally */
long get_user_pages_unlocked(unsigned long start, unsigned long nr_pages,
			     struct page **pages, unsigned int gup_flags)
{
	return -EINVAL;
}


/* Stubbed - not used externally */
int get_user_pages_fast_only(unsigned long start, int nr_pages,
			     unsigned int gup_flags, struct page **pages)
{
	return 0;
}

/* Stubbed - not used externally */
int get_user_pages_fast(unsigned long start, int nr_pages,
			unsigned int gup_flags, struct page **pages)
{
	return -EINVAL;
}

/* Stubbed - not used externally */
int pin_user_pages_fast(unsigned long start, int nr_pages,
			unsigned int gup_flags, struct page **pages)
{
	return -EINVAL;
}

/* Stubbed - not used externally */
int pin_user_pages_fast_only(unsigned long start, int nr_pages,
			     unsigned int gup_flags, struct page **pages)
{
	return 0;
}

/* Stubbed - not used externally */
long pin_user_pages_remote(struct mm_struct *mm,
			   unsigned long start, unsigned long nr_pages,
			   unsigned int gup_flags, struct page **pages,
			   struct vm_area_struct **vmas, int *locked)
{
	return -EINVAL;
}

/* Stubbed - not used externally */
long pin_user_pages(unsigned long start, unsigned long nr_pages,
		    unsigned int gup_flags, struct page **pages,
		    struct vm_area_struct **vmas)
{
	return -EINVAL;
}

/* Stubbed - not used externally */
long pin_user_pages_unlocked(unsigned long start, unsigned long nr_pages,
			     struct page **pages, unsigned int gup_flags)
{
	return -EINVAL;
}
