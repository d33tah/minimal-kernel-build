 
#ifndef _ASM_X86_PGTABLE_H
#define _ASM_X86_PGTABLE_H

#include <linux/mem_encrypt.h>
#include <asm/page.h>
#include <asm/pgtable_types.h>


/* pgprot_noncached removed - its only refs were the dead pgprot_writecombine /
 * pgprot_device fallback macros in linux/pgtable.h (0 callers) */

#ifndef __ASSEMBLY__
#include <linux/spinlock.h>
#include <asm/x86_init.h>
#include <asm/fpu/api.h>

/* Inlined from asm/pkru.h */
#include <asm/cpufeature.h>
/* End of pkru.h */
/* --- 2025-12-07 20:42 --- Inlined coco.h */
#include <asm/types.h>





 
extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)]
	__visible;
#define ZERO_PAGE(vaddr) ((void)(vaddr),virt_to_page(empty_zero_page))

extern spinlock_t pgd_lock;
extern struct list_head pgd_list;


#define set_pte(ptep, pte)		native_set_pte(ptep, pte)

#define set_pmd(pmdp, pmd)		native_set_pmd(pmdp, pmd)

/* --- 2026-06-18 --- __PAGETABLE_{P4D,PUD}_FOLDED arms are preprocessor-dead
 * on this 2-level build (all FOLDED macros #defined in pgtable_types.h, which
 * also supplies the folded set_pgd/pgd_clear/p4d_clear/pud_clear/p4d_val/
 * pud_val/pmd_val + __p4d/__pud/__pmd). Collapsed to the live (folded) arms. */

/* set_p4d / set_pud generic #ifndef fallbacks removed - both names are already
 * #defined in pgtable_types.h (folded set_p4d->set_pud->set_pmd) before this
 * point, so the #ifndef blocks were statically dead (and native_set_p4d /
 * native_set_pud were referenced only from inside them) */

#define pte_clear(mm, addr, ptep)	native_pte_clear(mm, addr, ptep)

#define pgd_val(x)	native_pgd_val(x)
#define __pgd(x)	native_make_pgd(x)

#define pte_val(x)	native_pte_val(x)
#define __pte(x)	native_make_pte(x)

#define arch_end_context_switch(prev)	do {} while(0)

 
static inline int pte_dirty(pte_t pte) {
	return pte_flags(pte) & _PAGE_DIRTY;
}

/* pte_young, pmd_dirty, pmd_young, pud_dirty, pud_young removed - unused */

static inline int pte_write(pte_t pte) {
	return pte_flags(pte) & _PAGE_RW;
}

/* pte_huge, pte_global removed - unused */

static inline int pte_exec(pte_t pte) {
	return !(pte_flags(pte) & _PAGE_NX);
}

static inline int pte_special(pte_t pte) {
	return pte_flags(pte) & _PAGE_SPECIAL;
}

 

static inline u64 protnone_mask(u64 val);

static inline unsigned long pte_pfn(pte_t pte) {
	phys_addr_t pfn = pte_val(pte);
	pfn ^= protnone_mask(pfn);
	return (pfn & PTE_PFN_MASK) >> PAGE_SHIFT;
}

/* pmd_pfn, pud_pfn, p4d_pfn, pgd_pfn removed - unused */

#define pte_page(pte)	pfn_to_page(pte_pfn(pte))

static inline int pmd_large(pmd_t pte) {
	return pmd_flags(pte) & _PAGE_PSE;
}


static inline pte_t pte_set_flags(pte_t pte, pteval_t set) {
	pteval_t v = native_pte_val(pte);

	return native_make_pte(v | set);
}

/* pte_clear_flags, pte_mkold, pte_wrprotect, pte_mkexec removed - unused */

static inline pte_t pte_mkdirty(pte_t pte) {
	return pte_set_flags(pte, _PAGE_DIRTY | _PAGE_SOFT_DIRTY);
}

static inline pte_t pte_mkyoung(pte_t pte) {
	return pte_set_flags(pte, _PAGE_ACCESSED);
}

static inline pte_t pte_mkwrite(pte_t pte) {
	return pte_set_flags(pte, _PAGE_RW);
}

/* pte_mkhuge, pte_clrhuge, pte_mkglobal, pte_clrglobal removed - unused */

/* pte_mkspecial, pte_mkdevmap removed - unused */

/* pmd_set_flags removed - unused */


/* pmd_mkold, pmd_mkclean removed - unused */


/* pmd_mkdirty removed - unused */

/* pmd_mkdevmap, pmd_mkhuge, pmd_mkyoung removed - unused */


/* pud_set_flags, pud_clear_flags removed - unused */
/* pud_mkold, pud_mkclean, pud_wrprotect, pud_mkdirty, pud_mkdevmap, pud_mkhuge, pud_mkyoung, pud_mkwrite removed - unused */


 
static inline pgprotval_t massage_pgprot(pgprot_t pgprot) {
	pgprotval_t protval = pgprot_val(pgprot);

	if (protval & _PAGE_PRESENT)
		protval &= __supported_pte_mask;

	return protval;
}

static inline pgprotval_t check_pgprot(pgprot_t pgprot) {
	pgprotval_t massaged_val = massage_pgprot(pgprot);

	 

	return massaged_val;
}

static inline pte_t pfn_pte(unsigned long page_nr, pgprot_t pgprot) {
	phys_addr_t pfn = (phys_addr_t)page_nr << PAGE_SHIFT;
	pfn ^= protnone_mask(pgprot_val(pgprot));
	pfn &= PTE_PFN_MASK;
	return __pte(pfn | check_pgprot(pgprot));
}

static inline pmd_t pfn_pmd(unsigned long page_nr, pgprot_t pgprot) {
	phys_addr_t pfn = (phys_addr_t)page_nr << PAGE_SHIFT;
	pfn ^= protnone_mask(pgprot_val(pgprot));
	pfn &= PHYSICAL_PMD_PAGE_MASK;
	return __pmd(pfn | check_pgprot(pgprot));
}

/* pfn_pud, pmd_mkinvalid removed - unused */

/* pte_modify, pmd_modify, pgprot_modify, flip_protnone_guard removed - unused */


pte_t *populate_extra_pte(unsigned long vaddr);

/* pti_set_user_pgtbl removed - unused */

#endif	 


# include <asm/pgtable_32.h>

#ifndef __ASSEMBLY__
#include <linux/mm_types.h>
#include <linux/mmdebug.h>
#include <linux/log2.h>
#include <asm/fixmap.h>

static inline int pte_none(pte_t pte) {
	return !(pte.pte & ~(_PAGE_KNL_ERRATUM_MASK));
}

static inline int pte_same(pte_t a, pte_t b) {
	return a.pte == b.pte;
}

static inline int pte_present(pte_t a) {
	return pte_flags(a) & (_PAGE_PRESENT | _PAGE_PROTNONE);
}


/* pte_accessible removed - unused */

static inline int pmd_present(pmd_t pmd) {
	 
	return pmd_flags(pmd) & (_PAGE_PRESENT | _PAGE_PROTNONE | _PAGE_PSE);
}


static inline int pmd_none(pmd_t pmd) {
	 
	unsigned long val = native_pmd_val(pmd);
	return (val & ~_PAGE_KNL_ERRATUM_MASK) == 0;
}

static inline unsigned long pmd_page_vaddr(pmd_t pmd) {
	return (unsigned long)__va(pmd_val(pmd) & pmd_pfn_mask(pmd));
}


#define mk_pte(page, pgprot)   pfn_pte(page_to_pfn(page), (pgprot))

static inline int pmd_bad(pmd_t pmd) {
	return (pmd_flags(pmd) & ~_PAGE_USER) != _KERNPG_TABLE;
}

/*
 * CONFIG_PGTABLE_LEVELS == 2 here: PMD/PUD/P4D are folded onto the PGD, so the
 * pud/p4d/pgd accessors for the LEVELS greater-than-2/3/4 cases were never
 * compiled. The folded inline definitions (incl. the always-0 pud_leaf) come
 * from the inlined nopmd/nopud/nop4d content in pgtable_types.h.
 */

/* p4d_index removed - unused */

#endif

#define KERNEL_PGD_BOUNDARY	pgd_index(PAGE_OFFSET)
#define KERNEL_PGD_PTRS		(PTRS_PER_PGD - KERNEL_PGD_BOUNDARY)

#ifndef __ASSEMBLY__

void init_mem_mapping(void);
void early_alloc_pgt_buf(void);
void __init poking_init(void);
unsigned long init_memory_mapping(unsigned long start, unsigned long end, pgprot_t prot);


 
static inline void set_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep, pte_t pte) {
	set_pte(ptep, pte);
}



 
struct vm_area_struct;

extern int ptep_set_access_flags(struct vm_area_struct *vma, unsigned long address, pte_t *ptep, pte_t entry, int dirty);

/* ptep_test_and_clear_young / ptep_clear_flush_young removed - no callers */

/* ptep_get_and_clear, ptep_get_and_clear_full, ptep_set_wrprotect removed - unused */



/* pmdp_set_access_flags, pudp_set_access_flags, pmdp_test_and_clear_young,
   pudp_test_and_clear_young, pmdp_clear_flush_young removed - unused */


/* pmd_write, pud_write removed - unused (0 callers; dragged pud_flags/pud_flags_mask/pud_pfn_mask dead) */

/* pmdp_establish, pmdp_invalidate_ad, pgdp_maps_userspace, pgd_large removed - unused */


static inline void clone_pgd_range(pgd_t *dst, pgd_t *src, int count) {
	memcpy(dst, src, count * sizeof(pgd_t));
}

/* page_level_shift, page_level_size, page_level_mask removed - unused */

/* update_mmu_cache removed - no-op on x86, all callers dropped */
/* update_mmu_cache_pmd, update_mmu_cache_pud removed - unused */
/* pte_swp_exclusive, pte_swp_clear_exclusive removed - unused (entire _PAGE_SWP_EXCLUSIVE block) */



/* pte_flags_pkey, __pkru_allows_pkey, __pte_access_permitted, pte_access_permitted removed - unused */

/* pmd_access_permitted, pud_access_permitted removed - unused */

/* pfn_modify_allowed removed - sole caller vmf_insert_pfn_prot removed */

/* arch_has_pfn_modify_check, arch_faults_on_old_pte removed - unused */

#endif

#endif  
