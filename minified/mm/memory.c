
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/sched/mm.h>
/* linux/sched/coredump.h removed - unused */
#include <linux/sched/task.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
/* linux/memremap.h removed - unused */
#include <linux/rmap.h>
#include <linux/init.h>
/* pfn_t.h removed - all functions inlined */
#define PFN_FLAGS_MASK \
	(((u64)(~PAGE_MASK)) << (BITS_PER_LONG_LONG - PAGE_SHIFT))
#define PFN_DEV (1ULL << (BITS_PER_LONG_LONG - 3))
/* memcontrol.h removed - unused */
/* mmu_notifier.h removed - inlined into asm/tlb.h, included below */
/* swapops.h removed - was empty */
#include <linux/gfp.h>
/* linux/migrate.h removed - empty header */
/* userfaultfd_k.h removed - empty stubs */
#include <linux/file.h>
/* numa.h, perf_event.h removed - unused */
#include <linux/vmalloc.h>

#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <linux/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

#include "internal.h"
/* struct swap_iocb forward decl removed - unused */

/* LAST_CPUPID warning removed - not triggered on x86 tinyconfig */

unsigned long max_mapnr;

struct page *mem_map;

static vm_fault_t do_fault(struct vm_fault *vmf);

void *high_memory;

/* randomize_va_space removed - no readers */

unsigned long zero_pfn __read_mostly;

unsigned long highest_memmap_pfn __read_mostly;

static int __init init_zero_pfn(void)
{
	zero_pfn = page_to_pfn(ZERO_PAGE(0));
	return 0;
}
early_initcall(init_zero_pfn);

/* rss_stat counter macros removed - counters are write-only */

static inline void free_pmd_range(struct mmu_gather *tlb, pud_t *pud,
				  unsigned long addr, unsigned long end,
				  unsigned long floor, unsigned long ceiling)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long start;

	start = addr;
	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		/* Inlined free_pte_range */
		{
			pgtable_t token = pmd_pgtable(*pmd);
			pmd_clear(pmd);
			pte_free_tlb(tlb, token, addr);
			/* mm_dec_nr_ptes inlined - single caller */
			atomic_long_sub(PTRS_PER_PTE * sizeof(pte_t),
					&tlb->mm->pgtables_bytes);
		}
	} while (pmd++, addr = next, addr != end);

	start &= PUD_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PUD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pmd = pmd_offset(pud, start);
	/* pud_clear removed - empty stub */
	pmd_free_tlb(tlb, pmd, start);
}

static inline void free_pud_range(struct mmu_gather *tlb, p4d_t *p4d,
				  unsigned long addr, unsigned long end,
				  unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

	/* pud_none_or_clear_bad always returns 0 - folded paging */
	start = addr;
	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		free_pmd_range(tlb, pud, addr, next, floor, ceiling);
	} while (pud++, addr = next, addr != end);

	start &= P4D_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= P4D_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pud = pud_offset(p4d, start);
	/* p4d_clear removed - empty stub */
	pud_free_tlb(tlb, pud, start);
}

static inline void free_p4d_range(struct mmu_gather *tlb, pgd_t *pgd,
				  unsigned long addr, unsigned long end,
				  unsigned long floor, unsigned long ceiling)
{
	p4d_t *p4d;
	unsigned long next;
	unsigned long start;

	/* p4d_none_or_clear_bad always returns 0 - folded paging */
	start = addr;
	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		free_pud_range(tlb, p4d, addr, next, floor, ceiling);
	} while (p4d++, addr = next, addr != end);

	start &= PGDIR_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PGDIR_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	p4d = p4d_offset(pgd, start);
	/* pgd_clear removed - empty stub */
	p4d_free_tlb(tlb, p4d, start);
}

void free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
		    unsigned long end, unsigned long floor,
		    unsigned long ceiling)
{
	pgd_t *pgd;
	unsigned long next;

	addr &= PMD_MASK;
	if (addr < floor) {
		addr += PMD_SIZE;
		if (!addr)
			return;
	}
	if (ceiling) {
		ceiling &= PMD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		end -= PMD_SIZE;
	if (addr > end - 1)
		return;

	/* pgd_none_or_clear_bad always returns 0 - folded paging */
	tlb_change_page_size(tlb, PAGE_SIZE);
	pgd = pgd_offset(tlb->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		free_p4d_range(tlb, pgd, addr, next, floor, ceiling);
	} while (pgd++, addr = next, addr != end);
}

void free_pgtables(struct mmu_gather *tlb, struct vm_area_struct *vma,
		   unsigned long floor, unsigned long ceiling)
{
	while (vma) {
		struct vm_area_struct *next = vma->vm_next;
		unsigned long addr = vma->vm_start;

		unlink_anon_vmas(vma);
		unlink_file_vma(vma);

		while (next && next->vm_start <= vma->vm_end + PMD_SIZE) {
			vma = next;
			next = vma->vm_next;
			unlink_anon_vmas(vma);
			unlink_file_vma(vma);
		}
		free_pgd_range(tlb, addr, vma->vm_end, floor,
			       next ? next->vm_start : ceiling);
		vma = next;
	}
}

static void pmd_install(struct mm_struct *mm, pmd_t *pmd, pgtable_t *pte)
{
	/* pmd_lock + pmd_lockptr inlined */
	spinlock_t *ptl = &mm->page_table_lock;
	spin_lock(ptl);

	if (likely(pmd_none(*pmd))) {
		/* mm_inc_nr_ptes inlined - single caller */
		atomic_long_add(PTRS_PER_PTE * sizeof(pte_t),
				&mm->pgtables_bytes);
		smp_wmb();
		pmd_populate(mm, pmd, *pte);
		*pte = NULL;
	}
	spin_unlock(ptl);
}

int __pte_alloc(struct mm_struct *mm, pmd_t *pmd)
{
	pgtable_t new = pte_alloc_one(mm);
	if (!new)
		return -ENOMEM;

	pmd_install(mm, pmd, &new);
	if (new)
		pte_free(mm, new);
	return 0;
}

int __pte_alloc_kernel(pmd_t *pmd)
{
	pte_t *new = pte_alloc_one_kernel(&init_mm);
	if (!new)
		return -ENOMEM;

	spin_lock(&init_mm.page_table_lock);
	if (likely(pmd_none(*pmd))) {
		smp_wmb();
		pmd_populate_kernel(&init_mm, pmd, new);
		new = NULL;
	}
	spin_unlock(&init_mm.page_table_lock);
	if (new)
		pte_free_kernel(&init_mm, new);
	return 0;
}

struct page *vm_normal_page(struct vm_area_struct *vma, unsigned long addr,
			    pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);

	/* CONFIG_ARCH_HAS_PTE_SPECIAL is always enabled */
	if (likely(!pte_special(pte)))
		goto check_pfn;
	/* find_special_page removed - never set */
	if (vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP))
		return NULL;
	if (is_zero_pfn(pfn))
		return NULL;
	/* pte_devmap always returns 0, check removed */
	return NULL;

	/* Dead code removed: the #else branch of CONFIG_ARCH_HAS_PTE_SPECIAL
	   and is_zero_pfn check were unreachable since the config is always set */

check_pfn:
	if (unlikely(pfn > highest_memmap_pfn))
		return NULL;

	/* 'out' label removed - unused */
	return pfn_to_page(pfn);
}

/* All copy_*_range functions removed - copy_page_range is stubbed
 * since simplified dup_mmap doesn't use them (~230 LOC removed)
 */

struct zap_details {
	struct folio *single_folio;
	bool even_cows;
	zap_flags_t zap_flags;
};

/* zap_pte_range, zap_pmd_range, zap_pud_range inlined */

static inline unsigned long zap_p4d_range(struct mmu_gather *tlb,
					  struct vm_area_struct *vma,
					  pgd_t *pgd, unsigned long addr,
					  unsigned long end,
					  struct zap_details *details)
{
	p4d_t *p4d;
	unsigned long next;

	/* p4d_none_or_clear_bad always returns 0 - folded paging */
	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		/* Inlined zap_pud_range */
		{
			pud_t *pud;
			unsigned long pud_next;
			unsigned long pud_addr = addr;
			pud = pud_offset(p4d, pud_addr);
			do {
				pud_next = pud_addr_end(pud_addr, next);
				/* zap_pmd_range inlined */
				{
					pmd_t *pmd = pmd_offset(pud, pud_addr);
					unsigned long pmd_next,
						pmd_addr = pud_addr;
					do {
						pmd_next = pmd_addr_end(
							pmd_addr, pud_next);
						if (!pmd_none_or_trans_huge_or_clear_bad(
							    pmd)) {
							spinlock_t *ptl;
							pte_t *pte =
								pte_offset_map_lock(
									tlb->mm,
									pmd,
									pmd_addr,
									&ptl);
							pte_unmap_unlock(pte,
									 ptl);
						}
						cond_resched();
					} while (pmd++, pmd_addr = pmd_next,
						 pmd_addr != pud_next);
					pud_next = pmd_addr;
				}
				cond_resched();
			} while (pud++, pud_addr = pud_next, pud_addr != next);
			next = pud_addr;
		}
	} while (p4d++, addr = next, addr != end);

	return addr;
}

static void unmap_page_range(struct mmu_gather *tlb, struct vm_area_struct *vma,
			     unsigned long addr, unsigned long end,
			     struct zap_details *details)
{
	pgd_t *pgd;
	unsigned long next;

	/* pgd_none_or_clear_bad always returns 0 - folded paging */
	BUG_ON(addr >= end);
	tlb_start_vma(tlb, vma);
	pgd = pgd_offset(vma->vm_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		next = zap_p4d_range(tlb, vma, pgd, addr, next, details);
	} while (pgd++, addr = next, addr != end);
	tlb_end_vma(tlb, vma);
}

static void unmap_single_vma(struct mmu_gather *tlb, struct vm_area_struct *vma,
			     unsigned long start_addr, unsigned long end_addr,
			     struct zap_details *details)
{
	unsigned long start = max(vma->vm_start, start_addr);
	unsigned long end;

	if (start >= vma->vm_end)
		return;
	end = min(vma->vm_end, end_addr);
	if (end <= vma->vm_start)
		return;
	/* untrack_pfn is empty stub, call removed */
	if (start != end)
		unmap_page_range(tlb, vma, start, end, details);
}

void unmap_vmas(struct mmu_gather *tlb, struct vm_area_struct *vma,
		unsigned long start_addr, unsigned long end_addr)
{
	struct mmu_notifier_range range;
	struct zap_details details = {
		.zap_flags = ZAP_FLAG_DROP_MARKER,

		.even_cows = true,
	};

	mmu_notifier_range_init(&range, MMU_NOTIFY_UNMAP, 0, vma, vma->vm_mm,
				start_addr, end_addr);
	mmu_notifier_invalidate_range_start(&range);
	for (; vma && vma->vm_start < end_addr; vma = vma->vm_next)
		unmap_single_vma(tlb, vma, start_addr, end_addr, &details);
	mmu_notifier_invalidate_range_end(&range);
}

pte_t *__get_locked_pte(struct mm_struct *mm, unsigned long addr,
			spinlock_t **ptl)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return NULL;
	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return NULL;
	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return NULL;
	return pte_alloc_map_lock(mm, pmd, addr, ptl);
}

/* insert_pfn inlined into vmf_insert_pfn_prot */

static vm_fault_t vmf_insert_pfn_prot(struct vm_area_struct *vma,
				      unsigned long addr, unsigned long pfn,
				      pgprot_t pgprot)
{
	BUG_ON(!(vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP)));
	BUG_ON((vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP)) ==
	       (VM_PFNMAP | VM_MIXEDMAP));
	BUG_ON((vma->vm_flags & VM_PFNMAP) && is_cow_mapping(vma->vm_flags));
	BUG_ON((vma->vm_flags & VM_MIXEDMAP) && pfn_valid(pfn));

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return VM_FAULT_SIGBUS;

	/* pfn_modify_allowed check removed - always returned true */
	/* track_pfn_insert is empty stub, call removed */
	/* Inlined insert_pfn */
	{
		struct mm_struct *mm = vma->vm_mm;
		pte_t *pte, entry;
		spinlock_t *ptl;
		/* __pfn_to_pfn_t, pfn_t_pte, pfn_t_to_pfn all inlined */
		pfn_t pfn_val = {
			.val = pfn | (PFN_DEV & PFN_FLAGS_MASK),
		};

		pte = get_locked_pte(mm, addr, &ptl);
		if (!pte)
			return VM_FAULT_OOM;
		if (!pte_none(*pte))
			goto pfn_out_unlock;

		entry = pte_mkspecial(
			pfn_pte(pfn_val.val & ~PFN_FLAGS_MASK, pgprot));
		set_pte_at(mm, addr, pte, entry);
pfn_out_unlock:
		pte_unmap_unlock(pte, ptl);
		return VM_FAULT_NOPAGE;
	}
}

vm_fault_t vmf_insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			  unsigned long pfn)
{
	return vmf_insert_pfn_prot(vma, addr, pfn, vma->vm_page_prot);
}

/* do_page_mkwrite inlined into do_shared_fault */

/* wp_page_reuse inlined into do_wp_page */

static vm_fault_t wp_page_copy(struct vm_fault *vmf)
{
	/* Minimal stub: init doesn't fork, so no COW faults */
	return VM_FAULT_OOM;
}

static vm_fault_t do_wp_page(struct vm_fault *vmf) __releases(vmf->ptl)
{
	vmf->page = vm_normal_page(vmf->vma, vmf->address, vmf->orig_pte);
	if (!vmf->page) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return wp_page_copy(vmf);
	}

	if (PageAnon(vmf->page) && PageAnonExclusive(vmf->page)) {
		pte_t entry;
		/* page_cpupid_xchg_last call removed - no effect (function returns page_to_nid, result unused) */
		entry = pte_mkyoung(vmf->orig_pte);
		entry = maybe_mkwrite(pte_mkdirty(entry), vmf->vma);
		ptep_set_access_flags(vmf->vma, vmf->address, vmf->pte, entry,
				      1);
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return VM_FAULT_WRITE;
	}

	get_page(vmf->page);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return wp_page_copy(vmf);
}

/* unmap_mapping_range_tree, unmap_mapping_folio, unmap_mapping_pages,
 * unmap_mapping_range removed - only callers were in truncate.c which was stubbed */

/* do_swap_page inlined - stub returns VM_FAULT_SIGBUS */

static vm_fault_t do_anonymous_page(struct vm_fault *vmf)
{
	/* Minimal stub: simplified anonymous page fault handling */
	struct vm_area_struct *vma = vmf->vma;
	struct page *page;
	pte_t entry;

	if (vma->vm_flags & VM_SHARED)
		return VM_FAULT_SIGBUS;

	if (pte_alloc(vma->vm_mm, vmf->pmd))
		return VM_FAULT_OOM;

	if (unlikely(anon_vma_prepare(vma)))
		return VM_FAULT_OOM;

	page = alloc_zeroed_user_highpage_movable(vma, vmf->address);
	if (!page)
		return VM_FAULT_OOM;

	__SetPageUptodate(page);
	entry = mk_pte(page, vma->vm_page_prot);
	if (vma->vm_flags & VM_WRITE)
		entry = pte_mkwrite(pte_mkdirty(entry));

	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, vmf->address,
				       &vmf->ptl);
	if (!pte_none(*vmf->pte)) {
		put_page(page);
		goto unlock;
	}

	/* inc_mm_counter_fast removed - rss_stat write-only */
	page_add_new_anon_rmap(page, vma, vmf->address);
	lru_cache_add_inactive_or_unevictable(page, vma);
	set_pte_at(vma->vm_mm, vmf->address, vmf->pte, entry);
	/* update_mmu_cache - empty stub on x86 */
unlock:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return 0;
}

static vm_fault_t __do_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret;

	if (pmd_none(*vmf->pmd) && !vmf->prealloc_pte) {
		vmf->prealloc_pte = pte_alloc_one(vma->vm_mm);
		if (!vmf->prealloc_pte)
			return VM_FAULT_OOM;
	}

	ret = vma->vm_ops->fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY |
			    VM_FAULT_DONE_COW)))
		return ret;

	/* PageHWPoison always returns false - removed dead code block */

	if (unlikely(!(ret & VM_FAULT_LOCKED)))
		lock_page(vmf->page);

	return ret;
}

/* do_set_pmd removed - never called */

static void do_set_pte(struct vm_fault *vmf, struct page *page,
		       unsigned long addr)
{
	struct vm_area_struct *vma = vmf->vma;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	pte_t entry;

	/* flush_icache_page - empty stub on x86 */
	entry = mk_pte(page, vma->vm_page_prot);

	entry = pte_sw_mkyoung(entry);

	if (write)
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
	/* pte_marker_uffd_wp() always false - userfaultfd disabled */

	if (write && !(vma->vm_flags & VM_SHARED)) {
		page_add_new_anon_rmap(page, vma, addr);
		lru_cache_add_inactive_or_unevictable(page, vma);
	} else {
		page_add_file_rmap(page, vma, false);
	}
	set_pte_at(vma->vm_mm, addr, vmf->pte, entry);
}

static vm_fault_t finish_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page;
	vm_fault_t ret;

	if ((vmf->flags & FAULT_FLAG_WRITE) && !(vma->vm_flags & VM_SHARED))
		page = vmf->cow_page;
	else
		page = vmf->page;

	/* check_stable_address_space always 0 - MMF_UNSTABLE never set */

	if (pmd_none(*vmf->pmd)) {
		/* PageTransCompound always returns false */
		if (vmf->prealloc_pte)
			pmd_install(vma->vm_mm, vmf->pmd, &vmf->prealloc_pte);
		else if (unlikely(pte_alloc(vma->vm_mm, vmf->pmd)))
			return VM_FAULT_OOM;
	}

	/* pmd_devmap_trans_unstable always returns 0 */

	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, vmf->address,
				       &vmf->ptl);
	ret = 0;

	if (likely((vmf->flags & FAULT_FLAG_ORIG_PTE_VALID) ?
			   pte_same(*vmf->pte, vmf->orig_pte) :
			   pte_none(*vmf->pte)))
		do_set_pte(vmf, page, vmf->address);
	else
		ret = VM_FAULT_NOPAGE;

	/* update_mmu_tlb removed - empty stub */
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return ret;
}

/* Removed: fault_around_bytes, do_fault_around, should_fault_around
 * - Optimization path always returned 0 for minimal kernel */

/* do_read_fault inlined into do_fault */

static vm_fault_t do_cow_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret;

	if (unlikely(anon_vma_prepare(vma)))
		return VM_FAULT_OOM;

	vmf->cow_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vmf->address);
	if (!vmf->cow_page)
		return VM_FAULT_OOM;
	/* mem_cgroup_charge always returns 0, cgroup_throttle_swaprate is empty */

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		goto uncharge_out;
	if (ret & VM_FAULT_DONE_COW)
		return ret;

	/* copy_user_highpage inlined - single caller */
	{
		char *vfrom = kmap_local_page(vmf->page);
		char *vto = kmap_local_page(vmf->cow_page);
		copy_user_page(vto, vfrom, vmf->address, vmf->cow_page);
		kunmap_local(vto);
		kunmap_local(vfrom);
	}
	__SetPageUptodate(vmf->cow_page);

	ret |= finish_fault(vmf);
	unlock_page(vmf->page);
	put_page(vmf->page);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		goto uncharge_out;
	return ret;
uncharge_out:
	put_page(vmf->cow_page);
	return ret;
}

/* Stub: read-only initramfs has no shared writable mappings */
/* do_shared_fault inlined - stub returns VM_FAULT_SIGBUS */

static vm_fault_t do_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mm_struct *vm_mm = vma->vm_mm;
	vm_fault_t ret;

	if (!vma->vm_ops->fault) {
		if (unlikely(!pmd_present(*vmf->pmd)))
			ret = VM_FAULT_SIGBUS;
		else {
			vmf->pte = pte_offset_map_lock(vmf->vma->vm_mm,
						       vmf->pmd, vmf->address,
						       &vmf->ptl);

			if (unlikely(pte_none(*vmf->pte)))
				ret = VM_FAULT_SIGBUS;
			else
				ret = VM_FAULT_NOPAGE;

			pte_unmap_unlock(vmf->pte, vmf->ptl);
		}
	} else if (!(vmf->flags & FAULT_FLAG_WRITE)) {
		/* Inlined do_read_fault */
		ret = __do_fault(vmf);
		if (likely(!(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE |
				    VM_FAULT_RETRY)))) {
			ret |= finish_fault(vmf);
			unlock_page(vmf->page);
			if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE |
					    VM_FAULT_RETRY)))
				put_page(vmf->page);
		}
	} else if (!(vma->vm_flags & VM_SHARED))
		ret = do_cow_fault(vmf);
	else
		ret = VM_FAULT_SIGBUS; /* do_shared_fault inlined */

	if (vmf->prealloc_pte) {
		pte_free(vm_mm, vmf->prealloc_pte);
		vmf->prealloc_pte = NULL;
	}
	return ret;
}

/* handle_pte_fault inlined into __handle_mm_fault */

static vm_fault_t __handle_mm_fault(struct vm_area_struct *vma,
				    unsigned long address, unsigned int flags)
{
	/* Minimal stub: simplified page fault handling without huge pages */
	struct vm_fault vmf = {
		.vma = vma,
		.address = address & PAGE_MASK,
		.real_address = address,
		.flags = flags,
		.pgoff = linear_page_index(vma, address),
		.gfp_mask = vma->vm_file ?
				    (mapping_gfp_mask(vma->vm_file->f_mapping) |
				     __GFP_FS | __GFP_IO) :
				    GFP_KERNEL,
	};
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	p4d_t *p4d;

	pgd = pgd_offset(mm, address);
	p4d = p4d_alloc(mm, pgd, address);
	if (!p4d)
		return VM_FAULT_OOM;

	vmf.pud = pud_alloc(mm, p4d, address);
	if (!vmf.pud)
		return VM_FAULT_OOM;

	vmf.pmd = pmd_alloc(mm, vmf.pud, address);
	if (!vmf.pmd)
		return VM_FAULT_OOM;

	/* handle_pte_fault inlined */
	if (unlikely(pmd_none(*vmf.pmd))) {
		vmf.pte = NULL;
	} else {
		vmf.pte = pte_offset_map(vmf.pmd, vmf.address);
		vmf.orig_pte = *vmf.pte;
		if (pte_none(vmf.orig_pte)) {
			pte_unmap(vmf.pte);
			vmf.pte = NULL;
		}
	}
	if (!vmf.pte)
		return vma_is_anonymous(vmf.vma) ? do_anonymous_page(&vmf) :
						   do_fault(&vmf);
	if (!pte_present(vmf.orig_pte))
		return VM_FAULT_SIGBUS; /* do_swap_page inlined - swap not configured */
	vmf.ptl = pte_lockptr(vmf.vma->vm_mm, vmf.pmd);
	spin_lock(vmf.ptl);
	if ((vmf.flags & FAULT_FLAG_WRITE) && !pte_write(vmf.orig_pte))
		return do_wp_page(&vmf);
	pte_unmap_unlock(vmf.pte, vmf.ptl);
	return 0;
}

vm_fault_t handle_mm_fault(struct vm_area_struct *vma, unsigned long address,
			   unsigned int flags, struct pt_regs *regs)
{
	vm_fault_t ret;

	__set_current_state(TASK_RUNNING);
	/* arch_vma_access_permitted always returns true (OSPKE disabled) */
	/* mem_cgroup_enter_user_fault removed - empty stub */
	ret = __handle_mm_fault(vma, address, flags);
	/* mem_cgroup_exit_user_fault, task_in_memcg_oom (always false),
	 * mem_cgroup_oom_synchronize removed - all stubs */
	/* current->maj_flt/min_flt increments removed - write-only fields */

	return ret;
}

/* USE_SPLIT_PTE_PTLOCKS is 0 (NR_CPUS=1 < SPLIT_PTLOCK_CPUS=4)
 * ptlock_cache_init, ptlock_alloc, ptlock_free removed as dead code */
