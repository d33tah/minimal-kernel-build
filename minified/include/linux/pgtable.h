#ifndef _LINUX_PGTABLE_H
#define _LINUX_PGTABLE_H

#include <linux/pfn.h>
#include <asm/pgtable.h>

#ifndef __ASSEMBLY__

#include <linux/mm_types.h>
#include <linux/bug.h>
#include <linux/errno.h>

#if 5 - defined(__PAGETABLE_P4D_FOLDED) - defined(__PAGETABLE_PUD_FOLDED) - \
	defined(__PAGETABLE_PMD_FOLDED) != CONFIG_PGTABLE_LEVELS
#error CONFIG_PGTABLE_LEVELS is not consistent with __PAGETABLE_{P4D,PUD,PMD}_FOLDED
#endif

#ifndef USER_PGTABLES_CEILING
#define USER_PGTABLES_CEILING	0UL
#endif

#ifndef FIRST_USER_ADDRESS
#define FIRST_USER_ADDRESS	0UL
#endif


static inline unsigned long pte_index(unsigned long address)
{
	return (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
}
#define pte_index pte_index

#ifndef pmd_index
static inline unsigned long pmd_index(unsigned long address)
{
	return (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
}
#define pmd_index pmd_index
#endif

/* pud_index removed - unused */

#ifndef pgd_index
#define pgd_index(a)  (((a) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
#endif

#ifndef pte_offset_kernel
static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long address)
{
	return (pte_t *)pmd_page_vaddr(*pmd) + pte_index(address);
}
#define pte_offset_kernel pte_offset_kernel
#endif

#define pte_offset_map(dir, address)	pte_offset_kernel((dir), (address))
#define pte_unmap(pte) ((void)(pte))	 

static inline pgd_t *pgd_offset_pgd(pgd_t *pgd, unsigned long address)
{
	return (pgd + pgd_index(address));
};

#ifndef pgd_offset
#define pgd_offset(mm, address)		pgd_offset_pgd((mm)->pgd, (address))
#endif

#ifndef pgd_offset_k
#define pgd_offset_k(address)		pgd_offset(&init_mm, (address))
#endif

#ifndef __HAVE_ARCH_UPDATE_MMU_TLB
static inline void update_mmu_tlb(struct vm_area_struct *vma,
				unsigned long address, pte_t *ptep)
{
}
#define __HAVE_ARCH_UPDATE_MMU_TLB
#endif


#ifndef pte_sw_mkyoung
static inline pte_t pte_sw_mkyoung(pte_t pte)
{
	return pte;
}
#define pte_sw_mkyoung	pte_sw_mkyoung
#endif






/* pmd_access_permitted, pud_access_permitted removed - 0 callers tree-wide */


/* set_{pmd,pud,p4d,pgd}_safe() + the pXd_same() predicates removed - unused */


#define pgd_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + PGDIR_SIZE) & PGDIR_MASK;	\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})

/* 2-level paging: pgd_bad()/p4d_bad()/pud_bad() are all constant 0 on x86
 * (pgtable_types.h), so the corruption arms of *_none_or_clear_bad() that
 * called pgd_clear_bad/p4d_clear_bad/pud_clear_bad (themselves no-ops) were
 * statically dead and have been folded out along with those no-op macros. */

/* *_none_or_clear_bad() (pgd/p4d/pud/pmd) removed - 0 callers tree-wide. */


/* pgprot_writecombine, pgprot_device removed - 0 callers tree-wide */

#ifndef __HAVE_ARCH_START_CONTEXT_SWITCH
#define arch_start_context_switch(prev)	do {} while (0)
#endif


#ifdef __HAVE_COLOR_ZERO_PAGE
static inline int is_zero_pfn(unsigned long pfn)
{
	extern unsigned long zero_pfn;
	unsigned long offset_from_zero_pfn = pfn - zero_pfn;
	return offset_from_zero_pfn <= (zero_page_mask >> PAGE_SHIFT);
}

/* my_zero_pfn removed - unused */

#else
static inline int is_zero_pfn(unsigned long pfn)
{
	extern unsigned long zero_pfn;
	return pfn == zero_pfn;
}

/* my_zero_pfn removed - unused */
#endif


static inline int pmd_trans_huge(pmd_t pmd)
{
	return 0;
}
/* pmd_read_atomic / pmd_none_or_trans_huge_or_clear_bad removed - 0 callers. */

/* p4d_set_huge, pud_set_huge, pmd_set_huge, p4d_free_pud_page, pud_free_pmd_page,
   pmd_free_pte_page, p4d_clear_huge, pud_clear_huge, pmd_clear_huge removed - unused */



extern void __init pgtable_cache_init(void);


#define		__PGTBL_PGD_MODIFIED	0
/* __PGTBL_P4D_MODIFIED (1), __PGTBL_PUD_MODIFIED (2) removed - P4D/PUD folded, never referenced */
#define		__PGTBL_PMD_MODIFIED	3
#define		__PGTBL_PTE_MODIFIED	4

#define		PGTBL_PGD_MODIFIED	BIT(__PGTBL_PGD_MODIFIED)
/* PGTBL_P4D_MODIFIED, PGTBL_PUD_MODIFIED removed - never referenced */
#define		PGTBL_PMD_MODIFIED	BIT(__PGTBL_PMD_MODIFIED)
#define		PGTBL_PTE_MODIFIED	BIT(__PGTBL_PTE_MODIFIED)

typedef unsigned int pgtbl_mod_mask;

#endif  

#if !defined(MAX_POSSIBLE_PHYSMEM_BITS) && !defined(CONFIG_64BIT)
#define MAX_POSSIBLE_PHYSMEM_BITS 32
#endif

#ifndef pud_leaf
#define pud_leaf(x)	0
#endif


#endif  
