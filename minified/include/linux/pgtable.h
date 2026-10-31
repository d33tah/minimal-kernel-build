#ifndef _LINUX_PGTABLE_H
#define _LINUX_PGTABLE_H

#include <asm/pgtable.h>

#if 5 - defined(__PAGETABLE_P4D_FOLDED) - defined(__PAGETABLE_PUD_FOLDED) - \
		defined(__PAGETABLE_PMD_FOLDED) !=                          \
	CONFIG_PGTABLE_LEVELS
#error CONFIG_PGTABLE_LEVELS is not consistent with __PAGETABLE_{P4D,PUD,PMD}_FOLDED
#endif

static inline unsigned long pte_index(unsigned long address)
{
	return (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
}

static inline unsigned long pmd_index(unsigned long address)
{
	return (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
}
#define pmd_index pmd_index

/* pud_index removed - unused */

#define pgd_index(a) (((a) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))

static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long address)
{
	return (pte_t *)pmd_page_vaddr(*pmd) + pte_index(address);
}
#define pte_offset_kernel pte_offset_kernel

#define pte_offset_map(dir, address) pte_offset_kernel((dir), (address))
#define pte_unmap(pte) ((void)(pte))

static inline pgd_t *pgd_offset_pgd(pgd_t *pgd, unsigned long address)
{
	return (pgd + pgd_index(address));
};

#define pgd_offset(mm, address) pgd_offset_pgd((mm)->pgd, (address))

static inline pte_t pte_sw_mkyoung(pte_t pte)
{
	return pte;
}
#define pte_sw_mkyoung pte_sw_mkyoung

/* pmd_access_permitted, pud_access_permitted removed - 0 callers tree-wide */

/* set_{pmd,pud,p4d,pgd}_safe() + the pXd_same() predicates removed - unused */

/* 2-level paging: pgd_bad()/p4d_bad()/pud_bad() are all constant 0 on x86
 * (pgtable_types.h), so the corruption arms of *_none_or_clear_bad() that
 * called pgd_clear_bad/p4d_clear_bad/pud_clear_bad (themselves no-ops) were
 * statically dead and have been folded out along with those no-op macros. */

/* *_none_or_clear_bad() (pgd/p4d/pud/pmd) removed - 0 callers tree-wide. */

/* pgprot_writecombine, pgprot_device removed - 0 callers tree-wide */

/* __HAVE_COLOR_ZERO_PAGE never defined tree-wide; dead #ifdef arm removed */
/* my_zero_pfn removed - unused */

/* pmd_read_atomic / pmd_none_or_trans_huge_or_clear_bad removed - 0 callers. */

/* p4d_set_huge, pud_set_huge, pmd_set_huge, p4d_free_pud_page, pud_free_pmd_page,
   pmd_free_pte_page, p4d_clear_huge, pud_clear_huge, pmd_clear_huge removed - unused */

#endif
