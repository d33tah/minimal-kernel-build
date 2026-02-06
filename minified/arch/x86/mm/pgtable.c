#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/hugetlb.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/fixmap.h>
/* mtrr.h removed - header is empty */

#define PGTABLE_HIGHMEM 0

/* paravirt_tlb_remove_table inlined */

gfp_t __userpte_alloc_gfp = GFP_PGTABLE_USER | PGTABLE_HIGHMEM;

pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	return __pte_alloc_one(mm, __userpte_alloc_gfp);
}

void ___pte_free_tlb(struct mmu_gather *tlb, struct page *pte)
{
	pgtable_pte_page_dtor(pte);
	paravirt_release_pte(page_to_pfn(pte));
	tlb_remove_page(tlb, pte);
}

/* pgd_list_add, pgd_list_del, _pgd_alloc, _pgd_free inlined into callers */

struct mm_struct *pgd_page_get_mm(struct page *page)
{
	return page->pt_mm;
}

/* PREALLOCATED_PMDS, MAX_PREALLOCATED_PMDS, PREALLOCATED_USER_PMDS, MAX_PREALLOCATED_USER_PMDS removed - never used (all 0 for 2-level paging) */

/* Simplified functions removed - free_pmds, preallocate_pmds, pgd_mop_up_pmds,
   pgd_prepopulate_pmd, pgd_prepopulate_user_pmd were empty stubs */

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;

	pgd = (pgd_t *)__get_free_pages(GFP_PGTABLE_USER, PGD_ALLOCATION_ORDER);
	if (pgd == NULL)
		return NULL;

	mm->pgd = pgd;

	spin_lock(&pgd_lock);
	clone_pgd_range(pgd + KERNEL_PGD_BOUNDARY,
			swapper_pg_dir + KERNEL_PGD_BOUNDARY, KERNEL_PGD_PTRS);
	virt_to_page(pgd)->pt_mm = mm;
	list_add(&virt_to_page(pgd)->lru, &pgd_list);
	spin_unlock(&pgd_lock);

	return pgd;
}

void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	spin_lock(&pgd_lock);
	list_del(&virt_to_page(pgd)->lru);
	spin_unlock(&pgd_lock);
	/* free_pages removed - empty stub */
}

int ptep_set_access_flags(struct vm_area_struct *vma, unsigned long address,
			  pte_t *ptep, pte_t entry, int dirty)
{
	int changed = !pte_same(*ptep, entry);

	if (changed && dirty)
		set_pte(ptep, entry);

	return changed;
}

/* ptep_test_and_clear_young, ptep_clear_flush_young removed - zero callers */

void __native_set_fixmap(enum fixed_addresses idx, pte_t pte)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	set_pte_vaddr(address, pte);
}

void native_set_fixmap(unsigned idx, phys_addr_t phys, pgprot_t flags)
{
	pgprot_val(flags) &= __default_kernel_pte_mask;

	__native_set_fixmap(idx, pfn_pte(phys >> PAGE_SHIFT, flags));
}
