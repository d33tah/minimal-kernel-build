
#include <linux/mm.h>
#include <linux/highmem.h>
#define PFN_FLAGS_MASK \
	(((u64)(~PAGE_MASK)) << (BITS_PER_LONG_LONG - PAGE_SHIFT))
#define PFN_DEV (1ULL << (BITS_PER_LONG_LONG - 3))

#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

#include "internal.h"

unsigned long max_mapnr;

struct page *mem_map;

static vm_fault_t do_fault(struct vm_fault *vmf);

void *high_memory;

unsigned long zero_pfn __read_mostly;

unsigned long highest_memmap_pfn __read_mostly;

static int __init init_zero_pfn(void)
{
	zero_pfn = page_to_pfn(ZERO_PAGE(0));
	return 0;
}
early_initcall(init_zero_pfn);

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
		{
			pgtable_t token = pmd_pgtable(*pmd);
			pmd_clear(pmd);
			pte_free_tlb(tlb, token, addr);
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
	pmd_free_tlb(tlb, pmd, start);
}

static inline void free_pud_range(struct mmu_gather *tlb, p4d_t *p4d,
				  unsigned long addr, unsigned long end,
				  unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

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
	pud_free_tlb(tlb, pud, start);
}

static inline void free_p4d_range(struct mmu_gather *tlb, pgd_t *pgd,
				  unsigned long addr, unsigned long end,
				  unsigned long floor, unsigned long ceiling)
{
	p4d_t *p4d;
	unsigned long next;
	unsigned long start;

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
	spinlock_t *ptl = &mm->page_table_lock;
	spin_lock(ptl);

	if (likely(pmd_none(*pmd))) {
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

struct page *vm_normal_page(struct vm_area_struct *vma, unsigned long addr,
			    pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);

	/* CONFIG_ARCH_HAS_PTE_SPECIAL is always enabled */
	if (likely(!pte_special(pte)))
		goto check_pfn;
	if (vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP))
		return NULL;
	if (is_zero_pfn(pfn))
		return NULL;
	return NULL;

check_pfn:
	if (unlikely(pfn > highest_memmap_pfn))
		return NULL;

	return pfn_to_page(pfn);
}

/* zap chain collapsed: zap_pte_range was a no-op (lock+unlock with no action),
 * so the entire zap_p4d_range -> unmap_page_range -> unmap_single_vma chain
 * is dead. unmap_vmas just does the mmu_notifier calls. */

void unmap_vmas(struct mmu_gather *tlb, struct vm_area_struct *vma,
		unsigned long start_addr, unsigned long end_addr)
{
	struct mmu_notifier_range range;
	mmu_notifier_range_init(&range, MMU_NOTIFY_UNMAP, 0, vma, vma->vm_mm,
				start_addr, end_addr);
	mmu_notifier_invalidate_range_start(&range);
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

static vm_fault_t do_wp_page(struct vm_fault *vmf) __releases(vmf->ptl)
{
	pte_t entry;
	vmf->page = vm_normal_page(vmf->vma, vmf->address, vmf->orig_pte);

	/* All writable anon pages should be AnonExclusive (no fork = no sharing) */
	entry = pte_mkyoung(vmf->orig_pte);
	entry = maybe_mkwrite(pte_mkdirty(entry), vmf->vma);
	ptep_set_access_flags(vmf->vma, vmf->address, vmf->pte, entry, 1);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return VM_FAULT_WRITE;
}

static vm_fault_t do_anonymous_page(struct vm_fault *vmf)
{
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

	page_add_new_anon_rmap(page, vma, vmf->address);
	lru_cache_add_inactive_or_unevictable(page, vma);
	set_pte_at(vma->vm_mm, vmf->address, vmf->pte, entry);
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

	if (unlikely(!(ret & VM_FAULT_LOCKED)))
		lock_page(vmf->page);

	return ret;
}

static void do_set_pte(struct vm_fault *vmf, struct page *page,
		       unsigned long addr)
{
	struct vm_area_struct *vma = vmf->vma;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	pte_t entry;

	entry = mk_pte(page, vma->vm_page_prot);

	entry = pte_sw_mkyoung(entry);

	if (write)
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);

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

	if (pmd_none(*vmf->pmd)) {
		if (vmf->prealloc_pte)
			pmd_install(vma->vm_mm, vmf->pmd, &vmf->prealloc_pte);
		else if (unlikely(pte_alloc(vma->vm_mm, vmf->pmd)))
			return VM_FAULT_OOM;
	}

	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, vmf->address,
				       &vmf->ptl);
	ret = 0;

	if (likely((vmf->flags & FAULT_FLAG_ORIG_PTE_VALID) ?
			   pte_same(*vmf->pte, vmf->orig_pte) :
			   pte_none(*vmf->pte)))
		do_set_pte(vmf, page, vmf->address);
	else
		ret = VM_FAULT_NOPAGE;

	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return ret;
}

static vm_fault_t do_cow_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret;

	if (unlikely(anon_vma_prepare(vma)))
		return VM_FAULT_OOM;

	vmf->cow_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vmf->address);
	if (!vmf->cow_page)
		return VM_FAULT_OOM;

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		goto uncharge_out;
	if (ret & VM_FAULT_DONE_COW)
		return ret;

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

static vm_fault_t __handle_mm_fault(struct vm_area_struct *vma,
				    unsigned long address, unsigned int flags)
{
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
	ret = __handle_mm_fault(vma, address, flags);
	return ret;
}
