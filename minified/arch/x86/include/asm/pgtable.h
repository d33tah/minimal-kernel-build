 
#ifndef _ASM_X86_PGTABLE_H
#define _ASM_X86_PGTABLE_H

#include <linux/mem_encrypt.h>
#include <asm/page.h>
#include <asm/pgtable_types.h>

 
#define pgprot_noncached(prot)						\
	((boot_cpu_data.x86 > 3)					\
	 ? (__pgprot(pgprot_val(prot) |					\
		     cachemode2protval(_PAGE_CACHE_MODE_UC_MINUS)))	\
	 : (prot))

#ifndef __ASSEMBLY__
#include <linux/spinlock.h>
#include <asm/x86_init.h>
#include <asm/fpu/api.h>

/* Inlined from asm/pkru.h */
#include <asm/cpufeature.h>
#define pkru_get_init_value()	0
static inline u32 read_pkru(void) { if (cpu_feature_enabled(X86_FEATURE_OSPKE)) return rdpkru(); return 0; }
static inline void pkru_write_default(void) { if (!cpu_feature_enabled(X86_FEATURE_OSPKE)) return; wrpkru(pkru_get_init_value()); }
/* End of pkru.h */
/* --- 2025-12-07 20:42 --- Inlined coco.h */
#include <asm/types.h>
/* cc_vendor enum, cc_set_vendor, cc_set_mask removed - unused */
static inline u64 cc_mkenc(u64 val)
{
	return val;
}
static inline u64 cc_mkdec(u64 val)
{
	return val;
}
#include <linux/page_table_check.h>

/* uffd stubs all removed - never called in any .c file */
/* early_top_pgt removed - unused (only declaration, no usage in .c or .S) */
/* __early_make_pgtable, ptdump_walk_* declarations removed - unused */

 
#define pgprot_encrypted(prot)	__pgprot(cc_mkenc(pgprot_val(prot)))
#define pgprot_decrypted(prot)	__pgprot(cc_mkdec(pgprot_val(prot)))
/* debug_checkwx, debug_checkwx_user removed - no-op stubs never used */

extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)]
	__visible;
#define ZERO_PAGE(vaddr) ((void)(vaddr),virt_to_page(empty_zero_page))

extern spinlock_t pgd_lock;
extern struct list_head pgd_list;

extern struct mm_struct *pgd_page_get_mm(struct page *page);

/* early_pmd_flags removed - unused */

#define set_pte(ptep, pte)		native_set_pte(ptep, pte)

#define set_pmd(pmdp, pmd)		native_set_pmd(pmdp, pmd)

#ifndef set_p4d
# define set_p4d(p4dp, p4d)		native_set_p4d(p4dp, p4d)
#endif

#ifndef set_pud
# define set_pud(pudp, pud)		native_set_pud(pudp, pud)
#endif

#define pte_clear(mm, addr, ptep)	native_pte_clear(mm, addr, ptep)
#define pmd_clear(pmd)			native_pmd_clear(pmd)

#define pgd_val(x)	native_pgd_val(x)
#define __pgd(x)	native_make_pgd(x)

#define pte_val(x)	native_pte_val(x)
#define __pte(x)	native_make_pte(x)

#define arch_end_context_switch(prev)	do {} while(0)

 
static inline int pte_dirty(pte_t pte)
{
	return pte_flags(pte) & _PAGE_DIRTY;
}

static inline int pte_young(pte_t pte)
{
	return pte_flags(pte) & _PAGE_ACCESSED;
}

/* pmd_dirty, pmd_young, pud_dirty, pud_young removed - unused */

static inline int pte_write(pte_t pte)
{
	return pte_flags(pte) & _PAGE_RW;
}

/* pte_huge, pte_global removed - unused */

static inline int pte_exec(pte_t pte)
{
	return !(pte_flags(pte) & _PAGE_NX);
}

static inline int pte_special(pte_t pte)
{
	return pte_flags(pte) & _PAGE_SPECIAL;
}

 

/* protnone_mask always returns 0 - forward decl removed */

static inline unsigned long pte_pfn(pte_t pte)
{
	phys_addr_t pfn = pte_val(pte);
	/* protnone_mask always returns 0 - xor removed */
	return (pfn & PTE_PFN_MASK) >> PAGE_SHIFT;
}

static inline unsigned long pmd_pfn(pmd_t pmd)
{
	phys_addr_t pfn = pmd_val(pmd);
	/* protnone_mask always returns 0 - xor removed */
	return (pfn & pmd_pfn_mask(pmd)) >> PAGE_SHIFT;
}

/* pud_pfn, p4d_pfn, pgd_pfn removed - unused */
/* p4d_large, p4d_leaf removed - never called */

#define pte_page(pte)	pfn_to_page(pte_pfn(pte))

#define pmd_leaf	pmd_large
static inline int pmd_large(pmd_t pte)
{
	return pmd_flags(pte) & _PAGE_PSE;
}


static inline pte_t pte_set_flags(pte_t pte, pteval_t set)
{
	pteval_t v = native_pte_val(pte);

	return native_make_pte(v | set);
}

static inline pte_t pte_clear_flags(pte_t pte, pteval_t clear)
{
	pteval_t v = native_pte_val(pte);

	return native_make_pte(v & ~clear);
}


/* pte_mkclean removed - unused */

static inline pte_t pte_mkold(pte_t pte)
{
	return pte_clear_flags(pte, _PAGE_ACCESSED);
}

/* pte_wrprotect, pte_mkexec removed - unused */

static inline pte_t pte_mkdirty(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_DIRTY | _PAGE_SOFT_DIRTY);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_ACCESSED);
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_RW);
}

/* pte_mkhuge, pte_clrhuge, pte_mkglobal, pte_clrglobal removed - unused */

static inline pte_t pte_mkspecial(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_SPECIAL);
}

/* pte_mkdevmap removed - pfn_t_devmap always returns false */

/* pmd_set_flags, pmd_mkdevmap removed - never called */
/* pmd_mkold, pmd_mkclean, pmd_wrprotect, pmd_mkdirty, pmd_mkhuge, pmd_mkyoung, pmd_mkwrite removed - unused */

/* pud_set_flags, pud_clear_flags removed - unused */
/* pud_mkold, pud_mkclean, pud_wrprotect, pud_mkdirty, pud_mkdevmap, pud_mkhuge, pud_mkyoung, pud_mkwrite removed - unused */


 
static inline pgprotval_t massage_pgprot(pgprot_t pgprot)
{
	pgprotval_t protval = pgprot_val(pgprot);

	if (protval & _PAGE_PRESENT)
		protval &= __supported_pte_mask;

	return protval;
}

static inline pgprotval_t check_pgprot(pgprot_t pgprot)
{
	pgprotval_t massaged_val = massage_pgprot(pgprot);

	 

	return massaged_val;
}

static inline pte_t pfn_pte(unsigned long page_nr, pgprot_t pgprot)
{
	phys_addr_t pfn = (phys_addr_t)page_nr << PAGE_SHIFT;
	/* protnone_mask always returns 0 - xor removed */
	pfn &= PTE_PFN_MASK;
	return __pte(pfn | check_pgprot(pgprot));
}

static inline pmd_t pfn_pmd(unsigned long page_nr, pgprot_t pgprot)
{
	phys_addr_t pfn = (phys_addr_t)page_nr << PAGE_SHIFT;
	/* protnone_mask always returns 0 - xor removed */
	pfn &= PHYSICAL_PMD_PAGE_MASK;
	return __pmd(pfn | check_pgprot(pgprot));
}

/* pfn_pud, pmd_mkinvalid, flip_protnone_guard, pte_modify, pmd_modify, pgprot_modify removed - unused */
/* pte_pgprot, pmd_pgprot, pud_pgprot, p4d_pgprot removed - unused */
/* canon_pgprot, is_new_memtype_allowed inlined at single call site */

pmd_t *populate_extra_pmd(unsigned long vaddr);
pte_t *populate_extra_pte(unsigned long vaddr);

/* pti_set_user_pgtbl removed - unused */

#endif	 


# include <asm/pgtable_32.h>

#ifndef __ASSEMBLY__
#include <linux/mm_types.h>
#include <linux/mmdebug.h>
#include <linux/log2.h>
#include <asm/fixmap.h>

static inline int pte_none(pte_t pte)
{
	return !(pte.pte & ~(_PAGE_KNL_ERRATUM_MASK));
}

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t a, pte_t b)
{
	return a.pte == b.pte;
}

static inline int pte_present(pte_t a)
{
	return pte_flags(a) & (_PAGE_PRESENT | _PAGE_PROTNONE);
}


/* pte_accessible removed - not called from any .c file */

static inline int pmd_present(pmd_t pmd)
{
	 
	return pmd_flags(pmd) & (_PAGE_PRESENT | _PAGE_PROTNONE | _PAGE_PSE);
}


static inline int pmd_none(pmd_t pmd)
{
	 
	unsigned long val = native_pmd_val(pmd);
	return (val & ~_PAGE_KNL_ERRATUM_MASK) == 0;
}

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)__va(pmd_val(pmd) & pmd_pfn_mask(pmd));
}

 
#define pmd_page(pmd)	pfn_to_page(pmd_pfn(pmd))

 
#define mk_pte(page, pgprot)   pfn_pte(page_to_pfn(page), (pgprot))

static inline int pmd_bad(pmd_t pmd)
{
	return (pmd_flags(pmd) & ~_PAGE_USER) != _KERNPG_TABLE;
}

static inline unsigned long pages_to_mb(unsigned long npg)
{
	return npg >> (20 - PAGE_SHIFT);
}

/* pud_large, pud_leaf removed - never called, CONFIG_PGTABLE_LEVELS == 2 */

#endif 

#define KERNEL_PGD_BOUNDARY	pgd_index(PAGE_OFFSET)
#define KERNEL_PGD_PTRS		(PTRS_PER_PGD - KERNEL_PGD_BOUNDARY)

#ifndef __ASSEMBLY__

extern int direct_gbpages;
void init_mem_mapping(void);
void early_alloc_pgt_buf(void);
void __init poking_init(void);
unsigned long init_memory_mapping(unsigned long start,
				  unsigned long end, pgprot_t prot);


 
static inline pte_t native_local_ptep_get_and_clear(pte_t *ptep)
{
	pte_t res = *ptep;

	 
	native_pte_clear(NULL, 0, ptep);
	return res;
}

static inline pmd_t native_local_pmdp_get_and_clear(pmd_t *pmdp)
{
	pmd_t res = *pmdp;

	native_pmd_clear(pmdp);
	return res;
}

static inline pud_t native_local_pudp_get_and_clear(pud_t *pudp)
{
	pud_t res = *pudp;

	native_pud_clear(pudp);
	return res;
}

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pte)
{
	page_table_check_pte_set(mm, addr, ptep, pte);
	set_pte(ptep, pte);
}

/* set_pmd_at, set_pud_at removed - unused */


struct vm_area_struct;

#define  __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
extern int ptep_set_access_flags(struct vm_area_struct *vma,
				 unsigned long address, pte_t *ptep,
				 pte_t entry, int dirty);

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
extern int ptep_test_and_clear_young(struct vm_area_struct *vma,
				     unsigned long addr, pte_t *ptep);

#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
extern int ptep_clear_flush_young(struct vm_area_struct *vma,
				  unsigned long address, pte_t *ptep);

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
				       pte_t *ptep)
{
	pte_t pte = native_ptep_get_and_clear(ptep);
	page_table_check_pte_clear(mm, addr, pte);
	return pte;
}

#define flush_tlb_fix_spurious_fault(vma, address) do { } while (0)

/* mk_pmd removed - unused */
/* pmdp_set_access_flags, pudp_set_access_flags, pmdp_test_and_clear_young,
   pudp_test_and_clear_young, pmdp_clear_flush_young removed - unused */


/* pmd_write, pmdp_huge_get_and_clear, pudp_huge_get_and_clear, pmdp_set_wrprotect, pud_write removed - unused */

/* pmdp_establish removed - never called */

/* pmdp_invalidate_ad, pgdp_maps_userspace, pgd_large removed - unused */


static inline void clone_pgd_range(pgd_t *dst, pgd_t *src, int count)
{
	memcpy(dst, src, count * sizeof(pgd_t));
}

/* page_level_shift, page_level_size, page_level_mask removed - unused */
/* update_mmu_cache, update_mmu_cache_pmd, update_mmu_cache_pud removed - unused */
/* pte_swp_exclusive, pte_swp_clear_exclusive removed - unused (entire _PAGE_SWP_EXCLUSIVE block) */



/* pte_flags_pkey, __pkru_allows_pkey, __pte_access_permitted,
   pte_access_permitted, pmd_access_permitted, pud_access_permitted,
   pfn_modify_allowed removed - never called or unused */
/* arch_has_pfn_modify_check, arch_faults_on_old_pte removed - unused */

#endif

#endif  
