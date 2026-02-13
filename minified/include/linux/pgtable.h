#ifndef _LINUX_PGTABLE_H
#define _LINUX_PGTABLE_H

#include <linux/pfn.h>
#include <asm/pgtable.h>

#ifndef __ASSEMBLY__

#include <linux/mm_types.h>
#include <linux/bug.h>

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

#ifndef pmd_pgtable
#define pmd_pgtable(pmd) pmd_page(pmd)
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

static inline pmd_t *pmd_off_k(unsigned long va)
{
	return pmd_offset(pud_offset(p4d_offset(pgd_offset_k(va), va), va), va);
}

static inline pte_t *virt_to_kpte(unsigned long vaddr)
{
	pmd_t *pmd = pmd_off_k(vaddr);

	return pmd_none(*pmd) ? NULL : pte_offset_kernel(pmd, vaddr);
}

/* ptep_set_access_flags - x86 defines __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS */
/* ptep_get_and_clear - x86 defines __HAVE_ARCH_PTEP_GET_AND_CLEAR */

#ifndef pte_sw_mkyoung
static inline pte_t pte_sw_mkyoung(pte_t pte)
{
	return pte;
}
#define pte_sw_mkyoung	pte_sw_mkyoung
#endif

/* pte_same - x86 defines __HAVE_ARCH_PTE_SAME */

#define pgd_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + PGDIR_SIZE) & PGDIR_MASK;	\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})

/* p4d_addr_end, pud_addr_end, pmd_addr_end - x86 overrides with #undef/#define */

static inline void pmd_clear_bad(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_clear(pmd);
}

static inline int pmd_none_or_clear_bad(pmd_t *pmd)
{
	if (pmd_none(*pmd))
		return 1;
	if (unlikely(pmd_bad(*pmd))) {
		pmd_clear_bad(pmd);
		return 1;
	}
	return 0;
}

/* pgprot_nx, pgprot_noncached - x86 defines its own versions */

static inline int is_zero_pfn(unsigned long pfn)
{
	extern unsigned long zero_pfn;
	return pfn == zero_pfn;
}

/* PAGE_KERNEL_RO, PAGE_KERNEL_EXEC - x86 defines its own */

#define		__PGTBL_PMD_MODIFIED	3
#define		__PGTBL_PTE_MODIFIED	4

#define		PGTBL_PMD_MODIFIED	BIT(__PGTBL_PMD_MODIFIED)
#define		PGTBL_PTE_MODIFIED	BIT(__PGTBL_PTE_MODIFIED)

typedef unsigned int pgtbl_mod_mask;

#endif  

#ifndef p4d_huge
#define p4d_huge(x)	0
#endif

#endif
