#include <linux/mm.h>
#include <linux/rmap.h>
#include <linux/file.h>

#include <asm/pgalloc.h>
#include <linux/swap.h>
#include <asm/tlbflush.h>

#include "internal.h"

unsigned long max_mapnr;

struct page *mem_map;

void *high_memory;

unsigned long highest_memmap_pfn __read_mostly;

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

	if (likely(!pte_special(pte)))
		goto check_pfn;
	/*
	 * A special PTE has no struct page. Nothing on this build creates one
	 * (no pte_mkspecial, no PFN-mapped VMAs, the zero page
	 * is never mapped into user space).
	 */
	return NULL;

check_pfn:
	if (unlikely(pfn > highest_memmap_pfn))
		return NULL;

	return pfn_to_page(pfn);
}

static gfp_t __get_fault_gfp_mask(struct vm_area_struct *vma)
{
	struct file *vm_file = vma->vm_file;

	if (vm_file)
		return mapping_gfp_mask(vm_file->f_mapping) | __GFP_FS |
		       __GFP_IO;

	return GFP_KERNEL;
}

static vm_fault_t do_anonymous_page(struct vm_fault *vmf)
{
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

	return ret;
}

static void do_set_pte(struct vm_fault *vmf, struct page *page,
		       unsigned long addr)
{
	struct vm_area_struct *vma = vmf->vma;
	pte_t entry;

	entry = mk_pte(page, vma->vm_page_prot);

	entry = pte_sw_mkyoung(entry);

	page_add_file_rmap(page, vma, false);
	set_pte_at(vma->vm_mm, addr, vmf->pte, entry);
}

static bool vmf_pte_changed(struct vm_fault *vmf)
{
	return !pte_none(*vmf->pte);
}

static vm_fault_t finish_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page;
	vm_fault_t ret;

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
		ret = VM_FAULT_SIGBUS;

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

	vmf->ptl = pte_lockptr(vmf->vma->vm_mm, vmf->pmd);
	spin_lock(vmf->ptl);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	/* No copy-on-write: a write to a read-only PTE is a broken invariant. */
	if ((vmf->flags & FAULT_FLAG_WRITE) && !pte_write(vmf->orig_pte))
		return VM_FAULT_SIGBUS;
	return 0;
}

static vm_fault_t __handle_mm_fault(struct vm_area_struct *vma,
				    unsigned long address, unsigned int flags)
{
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

	ret = __handle_mm_fault(vma, address, flags);

	return ret;
}
