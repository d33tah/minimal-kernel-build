
#include <asm/pgalloc.h>
#include <asm/tlb.h>

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

void free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
		    unsigned long end, unsigned long floor,
		    unsigned long ceiling)
{
	/* Stub: page table freeing not needed for Hello World kernel */
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

#define pte_alloc(mm, pmd) (unlikely(pmd_none(*(pmd))) && __pte_alloc(mm, pmd))

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

static vm_fault_t do_fault(struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

static vm_fault_t __handle_mm_fault(struct vm_area_struct *vma,
				    unsigned long address, unsigned int flags)
{
	struct vm_fault vmf = {
		.vma = vma,
		.address = address & PAGE_MASK,
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
