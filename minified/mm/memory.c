
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/rmap.h>
#include <linux/file.h>

#include <asm/pgalloc.h>
#include <linux/swap.h>
#include <asm/tlbflush.h>

#include "internal.h"

unsigned long max_mapnr;

struct page *mem_map;

void *high_memory;

unsigned long zero_pfn __read_mostly;

unsigned long highest_memmap_pfn __read_mostly;

static int __init init_zero_pfn(void)
{
	zero_pfn = page_to_pfn(ZERO_PAGE(0));
	return 0;
}
early_initcall(init_zero_pfn);

static void pmd_install(struct mm_struct *mm, pmd_t *pmd, pgtable_t *pte)
{
	spinlock_t *ptl = pmd_lock(mm, pmd);

	if (likely(pmd_none(*pmd))) {
		smp_wmb();
		pmd_populate(mm, pmd, *pte);
		*pte = NULL;
	}
	spin_unlock(ptl);
}

static int __pte_alloc(struct mm_struct *mm, pmd_t *pmd)
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

	/*
	 * x86 selects CONFIG_ARCH_HAS_PTE_SPECIAL unconditionally
	 * (arch/x86/Kconfig), so the IS_ENABLED() arm is always taken at compile
	 * time and the legacy !ARCH_HAS_PTE_SPECIAL fallback is dead.
	 */
	if (likely(!pte_special(pte)))
		goto check_pfn;
	/*
	 * vm_ops->find_special_page is never set by any vm_operations_struct on
	 * this build, so the pte_special path falls straight through to the
	 * VM_PFNMAP/zero_pfn/devmap checks below.
	 */
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

/* All copy_*_range functions removed - copy_page_range is stubbed
 * since simplified dup_mmap doesn't use them (~230 LOC removed)
 */

static gfp_t __get_fault_gfp_mask(struct vm_area_struct *vma)
{
	struct file *vm_file = vma->vm_file;

	if (vm_file)
		return mapping_gfp_mask(vm_file->f_mapping) | __GFP_FS |
		       __GFP_IO;

	return GFP_KERNEL;
}

static inline void wp_page_reuse(struct vm_fault *vmf) __releases(vmf->ptl)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page = vmf->page;
	pte_t entry;

	VM_BUG_ON(!(vmf->flags & FAULT_FLAG_WRITE));
	VM_BUG_ON(PageAnon(page) && !PageAnonExclusive(page));

	if (page)
		page_cpupid_xchg_last(page, (1 << LAST_CPUPID_SHIFT) - 1);

	entry = pte_mkyoung(vmf->orig_pte);
	entry = maybe_mkwrite(pte_mkdirty(entry), vma);
	ptep_set_access_flags(vma, vmf->address, vmf->pte, entry, 1);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
}

/*
 * Write fault on a present read-only PTE of a private mapping whose page is
 * not an exclusive anonymous page (e.g. a page-cache page mapped by a read
 * fault): copy it into a new anonymous page and map that writable. The caller
 * holds a reference on vmf->page (NULL for a special PTE: zero-fill).
 */
static vm_fault_t wp_page_copy(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *old_page = vmf->page;
	struct page *new_page;
	pte_t entry;

	if (old_page)
		new_page =
			alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vmf->address);
	else
		new_page =
			alloc_zeroed_user_highpage_movable(vma, vmf->address);
	if (!new_page) {
		if (old_page)
			put_page(old_page);
		return VM_FAULT_OOM;
	}
	if (old_page)
		copy_user_highpage(new_page, old_page, vmf->address, vma);
	__SetPageUptodate(new_page);

	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, vmf->address,
				       &vmf->ptl);
	if (pte_same(*vmf->pte, vmf->orig_pte)) {
		entry = mk_pte(new_page, vma->vm_page_prot);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		page_add_new_anon_rmap(new_page, vma, vmf->address);
		set_pte_at(vma->vm_mm, vmf->address, vmf->pte, entry);
		flush_tlb_one_user(vmf->address);
		if (old_page)
			page_remove_rmap(old_page);
		/* Drop the old PTE's reference below instead of the new page. */
		new_page = old_page;
	}
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	if (new_page)
		put_page(new_page);
	if (old_page)
		put_page(old_page);
	return VM_FAULT_WRITE;
}

static vm_fault_t do_wp_page(struct vm_fault *vmf) __releases(vmf->ptl)
{
	vmf->page = vm_normal_page(vmf->vma, vmf->address, vmf->orig_pte);
	if (!vmf->page) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return wp_page_copy(vmf);
	}

	if (PageAnon(vmf->page) && PageAnonExclusive(vmf->page)) {
		wp_page_reuse(vmf);
		return VM_FAULT_WRITE;
	}

	get_page(vmf->page);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return wp_page_copy(vmf);
}

static vm_fault_t do_anonymous_page(struct vm_fault *vmf)
{
	/* Minimal stub: simplified anonymous page fault handling */
	struct vm_area_struct *vma = vmf->vma;
	struct page *page;
	pte_t entry;

	if (pte_alloc(vma->vm_mm, vmf->pmd))
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
	else
		VM_BUG_ON_PAGE(!PageLocked(vmf->page), vmf->page);

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

	if (write) {
		page_add_new_anon_rmap(page, vma, addr);
	} else {
		page_add_file_rmap(page, vma, false);
	}
	set_pte_at(vma->vm_mm, addr, vmf->pte, entry);
}

static bool vmf_pte_changed(struct vm_fault *vmf)
{
	/* FAULT_FLAG_ORIG_PTE_VALID is never set on this build => always false. */
	return !pte_none(*vmf->pte);
}

static vm_fault_t finish_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page;
	vm_fault_t ret;

	if (vmf->flags & FAULT_FLAG_WRITE)
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

	if (likely(!vmf_pte_changed(vmf)))
		do_set_pte(vmf, page, vmf->address);
	else
		ret = VM_FAULT_NOPAGE;

	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return ret;
}

static vm_fault_t do_read_fault(struct vm_fault *vmf)
{
	vm_fault_t ret = 0;

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		return ret;

	ret |= finish_fault(vmf);
	unlock_page(vmf->page);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		put_page(vmf->page);
	return ret;
}

static vm_fault_t do_cow_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret;

	vmf->cow_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vmf->address);
	if (!vmf->cow_page)
		return VM_FAULT_OOM;

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		goto uncharge_out;
	if (ret & VM_FAULT_DONE_COW)
		return ret;

	copy_user_highpage(vmf->cow_page, vmf->page, vmf->address, vma);
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
	} else if (!(vmf->flags & FAULT_FLAG_WRITE))
		ret = do_read_fault(vmf);
	else
		ret = do_cow_fault(vmf);

	if (vmf->prealloc_pte) {
		pte_free(vm_mm, vmf->prealloc_pte);
		vmf->prealloc_pte = NULL;
	}
	return ret;
}

static vm_fault_t handle_pte_fault(struct vm_fault *vmf)
{
	if (unlikely(pmd_none(*vmf->pmd))) {
		vmf->pte = NULL;
	} else {
		vmf->pte = pte_offset_map(vmf->pmd, vmf->address);
		vmf->orig_pte = *vmf->pte;
		if (pte_none(vmf->orig_pte)) {
			pte_unmap(vmf->pte);
			vmf->pte = NULL;
		}
	}

	if (!vmf->pte)
		return vma_is_anonymous(vmf->vma) ? do_anonymous_page(vmf) :
						    do_fault(vmf);

	/*
	 * A mapped PTE is always present on this build: there is no swap,
	 * migration, NUMA-hinting or PTE-marker source of !present entries, so
	 * the legacy do_swap_page() path is unreachable and has been removed.
	 */
	vmf->ptl = pte_lockptr(vmf->vma->vm_mm, vmf->pmd);
	spin_lock(vmf->ptl);
	if ((vmf->flags & (FAULT_FLAG_WRITE | FAULT_FLAG_UNSHARE)) &&
	    !pte_write(vmf->orig_pte))
		return do_wp_page(vmf);

	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return 0;
}

static vm_fault_t __handle_mm_fault(struct vm_area_struct *vma,
				    unsigned long address, unsigned int flags)
{
	/* Minimal stub: simplified page fault handling without huge pages */
	struct vm_fault vmf = {
		.vma = vma,
		.address = address & PAGE_MASK,
		.flags = flags,
		.pgoff = linear_page_index(vma, address),
		.gfp_mask = __get_fault_gfp_mask(vma),
	};
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	p4d_t *p4d;

	pgd = pgd_offset(mm, address);
	p4d = p4d_alloc(mm, pgd, address);
	vmf.pud = pud_alloc(mm, p4d, address);
	vmf.pmd = pmd_alloc(mm, vmf.pud, address);

	return handle_pte_fault(&vmf);
}

vm_fault_t handle_mm_fault(struct vm_area_struct *vma, unsigned long address,
			   unsigned int flags, struct pt_regs *regs)
{
	vm_fault_t ret;

	__set_current_state(TASK_RUNNING);

	/* arch_vma_access_permitted() is constant-true (no PKU) => guard dropped. */
	ret = __handle_mm_fault(vma, address, flags);

	return ret;
}

/*
 * __p4d_alloc / __pud_alloc / __pmd_alloc were here. On this build all three
 * page-table levels are folded (CONFIG_PGTABLE_LEVELS=2, no PAE), so
 * __PAGETABLE_{P4D,PUD,PMD}_FOLDED are all defined and mm.h supplies the
 * inline `return 0` stubs that p4d_alloc/pud_alloc/pmd_alloc actually use.
 * The out-of-line versions were guarded by #ifndef __PAGETABLE_*_FOLDED and
 * thus never compiled or linked -- removed as dead code.
 */
