 
#ifndef _ASM_X86_PGALLOC_H
#define _ASM_X86_PGALLOC_H

#include <linux/threads.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

/* Inlined from asm-generic/pgalloc.h */
#define GFP_PGTABLE_KERNEL	(GFP_KERNEL | __GFP_ZERO)
#define GFP_PGTABLE_USER	(GFP_PGTABLE_KERNEL | __GFP_ACCOUNT)

/* pte_alloc_one_kernel + its sole callee __pte_alloc_one_kernel + pte_free_kernel removed - 0-caller kernel pte-table alloc/free wrappers (cascade cluster) */

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

static inline void pte_free(struct mm_struct *mm, struct page *pte_page)
{
	pgtable_pte_page_dtor(pte_page);
	__free_page(pte_page);
}


/*
 * PGTABLE_LEVELS == 2 (x86_32, no PAE): the PMD/PUD/P4D allocation and
 * freeing helpers guarded by CONFIG_PGTABLE_LEVELS > 2/3/4 are all folded
 * away and were removed wholesale.
 */

/* paravirt_alloc_pte/paravirt_alloc_pmd/paravirt_release_pte no-op stubs + their discarded calls removed - PARAVIRT off */


extern gfp_t __userpte_alloc_gfp;

#define PGD_ALLOCATION_ORDER 0

 
extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

extern pgtable_t pte_alloc_one(struct mm_struct *);

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	set_pmd(pmd, __pmd(__pa(pte) | _PAGE_TABLE));
}


static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *pte)
{
	unsigned long pfn = page_to_pfn(pte);

	set_pmd(pmd, __pmd(((pteval_t)pfn << PAGE_SHIFT) | _PAGE_TABLE));
}

#endif
