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

#ifndef pud_index
static inline unsigned long pud_index(unsigned long address)
{
	return (address >> PUD_SHIFT) & (PTRS_PER_PUD - 1);
}
#define pud_index pud_index
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


#ifndef __HAVE_ARCH_PMDP_SET_WRPROTECT
#endif
#ifndef __HAVE_ARCH_PUDP_SET_WRPROTECT
#endif




#ifndef pte_access_permitted
#define pte_access_permitted(pte, write) \
	(pte_present(pte) && (!(write) || pte_write(pte)))
#endif

#ifndef pmd_access_permitted
#define pmd_access_permitted(pmd, write) \
	(pmd_present(pmd) && (!(write) || pmd_write(pmd)))
#endif

#ifndef pud_access_permitted
#define pud_access_permitted(pud, write) \
	(pud_present(pud) && (!(write) || pud_write(pud)))
#endif


/* set_{pmd,pud,p4d,pgd}_safe() + the pXd_same() predicates removed - unused */


#ifndef __HAVE_ARCH_MOVE_PTE
#define move_pte(pte, prot, old_addr, new_addr)	(pte)
#endif

#ifndef pte_accessible
# define pte_accessible(mm, pte)	((void)(pte), 1)
#endif

#ifndef flush_tlb_fix_spurious_fault
#define flush_tlb_fix_spurious_fault(vma, address) flush_tlb_page(vma, address)
#endif


#define pgd_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + PGDIR_SIZE) & PGDIR_MASK;	\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})

#ifndef p4d_addr_end
#define p4d_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + P4D_SIZE) & P4D_MASK;	\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})
#endif

#ifndef pud_addr_end
#define pud_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + PUD_SIZE) & PUD_MASK;	\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})
#endif

#ifndef pmd_addr_end
#define pmd_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + PMD_SIZE) & PMD_MASK;	\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})
#endif

/* 2-level paging: pgd_bad()/p4d_bad()/pud_bad() are all constant 0 on x86
 * (pgtable_types.h), so the corruption arms of *_none_or_clear_bad() that
 * called pgd_clear_bad/p4d_clear_bad/pud_clear_bad (themselves no-ops) were
 * statically dead and have been folded out along with those no-op macros. */

void pmd_clear_bad(pmd_t *);

static inline int pgd_none_or_clear_bad(pgd_t *pgd)
{
	return pgd_none(*pgd) ? 1 : 0;
}

static inline int p4d_none_or_clear_bad(p4d_t *p4d)
{
	return p4d_none(*p4d) ? 1 : 0;
}

static inline int pud_none_or_clear_bad(pud_t *pud)
{
	return pud_none(*pud) ? 1 : 0;
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



#ifndef pgprot_nx
#define pgprot_nx(prot)	(prot)
#endif

#ifndef pgprot_noncached
#define pgprot_noncached(prot)	(prot)
#endif

#ifndef pgprot_writecombine
#define pgprot_writecombine pgprot_noncached
#endif

#ifndef pgprot_device
#define pgprot_device pgprot_noncached
#endif

#ifndef pgprot_modify
#define pgprot_modify pgprot_modify
static inline pgprot_t pgprot_modify(pgprot_t oldprot, pgprot_t newprot)
{
	if (pgprot_val(oldprot) == pgprot_val(pgprot_noncached(oldprot)))
		newprot = pgprot_noncached(newprot);
	if (pgprot_val(oldprot) == pgprot_val(pgprot_writecombine(oldprot)))
		newprot = pgprot_writecombine(newprot);
	if (pgprot_val(oldprot) == pgprot_val(pgprot_device(oldprot)))
		newprot = pgprot_device(newprot);
	return newprot;
}
#endif

#ifndef pgprot_encrypted
#define pgprot_encrypted(prot)	(prot)
#endif

#ifndef pgprot_decrypted
#define pgprot_decrypted(prot)	(prot)
#endif

#ifndef __HAVE_ARCH_ENTER_LAZY_MMU_MODE
#define arch_enter_lazy_mmu_mode()	do {} while (0)
#define arch_leave_lazy_mmu_mode()	do {} while (0)
#endif

#ifndef __HAVE_ARCH_START_CONTEXT_SWITCH
#define arch_start_context_switch(prev)	do {} while (0)
#endif


extern void untrack_pfn(struct vm_area_struct *vma, unsigned long pfn,
			unsigned long size);

#ifdef __HAVE_COLOR_ZERO_PAGE
static inline int is_zero_pfn(unsigned long pfn)
{
	extern unsigned long zero_pfn;
	unsigned long offset_from_zero_pfn = pfn - zero_pfn;
	return offset_from_zero_pfn <= (zero_page_mask >> PAGE_SHIFT);
}

#define my_zero_pfn(addr)	page_to_pfn(ZERO_PAGE(addr))

#else
static inline int is_zero_pfn(unsigned long pfn)
{
	extern unsigned long zero_pfn;
	return pfn == zero_pfn;
}

static inline unsigned long my_zero_pfn(unsigned long addr)
{
	extern unsigned long zero_pfn;
	return zero_pfn;
}
#endif


static inline int pmd_trans_huge(pmd_t pmd)
{
	return 0;
}
#ifndef pmd_write
static inline int pmd_write(pmd_t pmd)
{
	BUG();
	return 0;
}
#endif  

#ifndef pud_write
static inline int pud_write(pud_t pud)
{
	BUG();
	return 0;
}
#endif  

#ifndef pmd_read_atomic
static inline pmd_t pmd_read_atomic(pmd_t *pmdp)
{
	 
	return *pmdp;
}
#endif

static inline int pmd_none_or_trans_huge_or_clear_bad(pmd_t *pmd)
{
	pmd_t pmdval = pmd_read_atomic(pmd);
	 
	 
	if (pmd_none(pmdval) || pmd_trans_huge(pmdval) ||
		(IS_ENABLED(CONFIG_ARCH_ENABLE_THP_MIGRATION) && !pmd_present(pmdval)))
		return 1;
	if (unlikely(pmd_bad(pmdval))) {
		pmd_clear_bad(pmd);
		return 1;
	}
	return 0;
}

/* p4d_set_huge, pud_set_huge, pmd_set_huge, p4d_free_pud_page, pud_free_pmd_page,
   pmd_free_pte_page, p4d_clear_huge, pud_clear_huge, pmd_clear_huge removed - unused */



extern void __init pgtable_cache_init(void);

#ifndef PAGE_KERNEL_RO
# define PAGE_KERNEL_RO PAGE_KERNEL
#endif

#ifndef PAGE_KERNEL_EXEC
# define PAGE_KERNEL_EXEC PAGE_KERNEL
#endif

#define		__PGTBL_PGD_MODIFIED	0
#define		__PGTBL_P4D_MODIFIED	1
#define		__PGTBL_PUD_MODIFIED	2
#define		__PGTBL_PMD_MODIFIED	3
#define		__PGTBL_PTE_MODIFIED	4

#define		PGTBL_PGD_MODIFIED	BIT(__PGTBL_PGD_MODIFIED)
#define		PGTBL_P4D_MODIFIED	BIT(__PGTBL_P4D_MODIFIED)
#define		PGTBL_PUD_MODIFIED	BIT(__PGTBL_PUD_MODIFIED)
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
#ifndef pmd_leaf
#define pmd_leaf(x)	0
#endif


#endif  
