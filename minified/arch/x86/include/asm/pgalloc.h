 
#ifndef _ASM_X86_PGALLOC_H
#define _ASM_X86_PGALLOC_H

#include <linux/threads.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#define __HAVE_ARCH_PTE_ALLOC_ONE
#define __HAVE_ARCH_PGD_FREE

/* Inlined from asm-generic/pgalloc.h */
#define GFP_PGTABLE_KERNEL	(GFP_KERNEL | __GFP_ZERO)
#define GFP_PGTABLE_USER	(GFP_PGTABLE_KERNEL | __GFP_ACCOUNT)

static inline pte_t *__pte_alloc_one_kernel(struct mm_struct *mm)
{
	return (pte_t *)__get_free_page(GFP_PGTABLE_KERNEL);
}

#ifndef __HAVE_ARCH_PTE_ALLOC_ONE_KERNEL
static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	return __pte_alloc_one_kernel(mm);
}
#endif

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline pgtable_t __pte_alloc_one(struct mm_struct *mm, gfp_t gfp)
{
	struct page *pte;

	pte = alloc_page(gfp);
	if (!pte)
		return NULL;
	if (!pgtable_pte_page_ctor(pte)) {
		__free_page(pte);
		return NULL;
	}

	return pte;
}

#ifndef __HAVE_ARCH_PTE_ALLOC_ONE
static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	return __pte_alloc_one(mm, GFP_PGTABLE_USER);
}
#endif


static inline void pte_free(struct mm_struct *mm, struct page *pte_page)
{
	pgtable_pte_page_dtor(pte_page);
	__free_page(pte_page);
}


#if CONFIG_PGTABLE_LEVELS > 2

#ifndef __HAVE_ARCH_PMD_ALLOC_ONE
static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	struct page *page;
	gfp_t gfp = GFP_PGTABLE_USER;

	if (mm == &init_mm)
		gfp = GFP_PGTABLE_KERNEL;
	page = alloc_pages(gfp, 0);
	if (!page)
		return NULL;
	if (!pgtable_pmd_page_ctor(page)) {
		__free_pages(page, 0);
		return NULL;
	}
	return (pmd_t *)page_address(page);
}
#endif

#ifndef __HAVE_ARCH_PMD_FREE
static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	BUG_ON((unsigned long)pmd & (PAGE_SIZE-1));
	pgtable_pmd_page_dtor(virt_to_page(pmd));
	free_page((unsigned long)pmd);
}
#endif

#endif

#if CONFIG_PGTABLE_LEVELS > 3

static inline pud_t *__pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	gfp_t gfp = GFP_PGTABLE_USER;

	if (mm == &init_mm)
		gfp = GFP_PGTABLE_KERNEL;
	return (pud_t *)get_zeroed_page(gfp);
}

#ifndef __HAVE_ARCH_PUD_ALLOC_ONE
static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return __pud_alloc_one(mm, addr);
}
#endif

static inline void __pud_free(struct mm_struct *mm, pud_t *pud)
{
	BUG_ON((unsigned long)pud & (PAGE_SIZE-1));
	free_page((unsigned long)pud);
}

#ifndef __HAVE_ARCH_PUD_FREE
static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	__pud_free(mm, pud);
}
#endif

#endif

#ifndef __HAVE_ARCH_PGD_FREE
static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}
#endif

static inline int  __paravirt_pgd_alloc(struct mm_struct *mm) { return 0; }

#define paravirt_pgd_alloc(mm)	__paravirt_pgd_alloc(mm)
static inline void paravirt_pgd_free(struct mm_struct *mm, pgd_t *pgd) {}
static inline void paravirt_alloc_pte(struct mm_struct *mm, unsigned long pfn)	{}
static inline void paravirt_alloc_pmd(struct mm_struct *mm, unsigned long pfn)	{}
/* paravirt_alloc_pmd_clone removed - unused */
static inline void paravirt_alloc_pud(struct mm_struct *mm, unsigned long pfn)	{}
static inline void paravirt_alloc_p4d(struct mm_struct *mm, unsigned long pfn)	{}
static inline void paravirt_release_pte(unsigned long pfn) {}
static inline void paravirt_release_pmd(unsigned long pfn) {}
static inline void paravirt_release_pud(unsigned long pfn) {}
static inline void paravirt_release_p4d(unsigned long pfn) {}

 
extern gfp_t __userpte_alloc_gfp;

#define PGD_ALLOCATION_ORDER 0

 
extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

extern pgtable_t pte_alloc_one(struct mm_struct *);

extern void ___pte_free_tlb(struct mmu_gather *tlb, struct page *pte);

static inline void __pte_free_tlb(struct mmu_gather *tlb, struct page *pte,
				  unsigned long address)
{
	___pte_free_tlb(tlb, pte);
}

static inline void pmd_populate_kernel(struct mm_struct *mm,
				       pmd_t *pmd, pte_t *pte)
{
	paravirt_alloc_pte(mm, __pa(pte) >> PAGE_SHIFT);
	set_pmd(pmd, __pmd(__pa(pte) | _PAGE_TABLE));
}

/* pmd_populate_kernel_safe removed - unused */

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				struct page *pte)
{
	unsigned long pfn = page_to_pfn(pte);

	paravirt_alloc_pte(mm, pfn);
	set_pmd(pmd, __pmd(((pteval_t)pfn << PAGE_SHIFT) | _PAGE_TABLE));
}

#if CONFIG_PGTABLE_LEVELS > 2
extern void ___pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd);

static inline void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd,
				  unsigned long address)
{
	___pmd_free_tlb(tlb, pmd);
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	paravirt_alloc_pmd(mm, __pa(pmd) >> PAGE_SHIFT);
	set_pud(pud, __pud(_PAGE_TABLE | __pa(pmd)));
}

/* pud_populate_safe removed - unused */

#if CONFIG_PGTABLE_LEVELS > 3
static inline void p4d_populate(struct mm_struct *mm, p4d_t *p4d, pud_t *pud)
{
	paravirt_alloc_pud(mm, __pa(pud) >> PAGE_SHIFT);
	set_p4d(p4d, __p4d(_PAGE_TABLE | __pa(pud)));
}

static inline void p4d_populate_safe(struct mm_struct *mm, p4d_t *p4d, pud_t *pud)
{
	paravirt_alloc_pud(mm, __pa(pud) >> PAGE_SHIFT);
	set_p4d_safe(p4d, __p4d(_PAGE_TABLE | __pa(pud)));
}

extern void ___pud_free_tlb(struct mmu_gather *tlb, pud_t *pud);

static inline void __pud_free_tlb(struct mmu_gather *tlb, pud_t *pud,
				  unsigned long address)
{
	___pud_free_tlb(tlb, pud);
}

#if CONFIG_PGTABLE_LEVELS > 4
static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, p4d_t *p4d)
{
	if (!pgtable_l5_enabled())
		return;
	paravirt_alloc_p4d(mm, __pa(p4d) >> PAGE_SHIFT);
	set_pgd(pgd, __pgd(_PAGE_TABLE | __pa(p4d)));
}

static inline void pgd_populate_safe(struct mm_struct *mm, pgd_t *pgd, p4d_t *p4d)
{
	if (!pgtable_l5_enabled())
		return;
	paravirt_alloc_p4d(mm, __pa(p4d) >> PAGE_SHIFT);
	set_pgd_safe(pgd, __pgd(_PAGE_TABLE | __pa(p4d)));
}

static inline p4d_t *p4d_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	gfp_t gfp = GFP_KERNEL_ACCOUNT;

	if (mm == &init_mm)
		gfp &= ~__GFP_ACCOUNT;
	return (p4d_t *)get_zeroed_page(gfp);
}

static inline void p4d_free(struct mm_struct *mm, p4d_t *p4d)
{
	if (!pgtable_l5_enabled())
		return;

	BUG_ON((unsigned long)p4d & (PAGE_SIZE-1));
	free_page((unsigned long)p4d);
}

extern void ___p4d_free_tlb(struct mmu_gather *tlb, p4d_t *p4d);

static inline void __p4d_free_tlb(struct mmu_gather *tlb, p4d_t *p4d,
				  unsigned long address)
{
	if (pgtable_l5_enabled())
		___p4d_free_tlb(tlb, p4d);
}

#endif	 
#endif	 
#endif	 

#endif  
