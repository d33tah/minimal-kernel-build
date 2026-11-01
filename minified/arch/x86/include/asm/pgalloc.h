#ifndef _ASM_X86_PGALLOC_H
#define _ASM_X86_PGALLOC_H

#include <linux/mm.h>

#define GFP_PGTABLE_KERNEL (GFP_KERNEL | __GFP_ZERO)
#define GFP_PGTABLE_USER (GFP_PGTABLE_KERNEL | __GFP_ACCOUNT)

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

#define PGD_ALLOCATION_ORDER 0

extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

extern pgtable_t pte_alloc_one(struct mm_struct *);

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				struct page *pte)
{
	unsigned long pfn = page_to_pfn(pte);

	set_pmd(pmd, __pmd(((pteval_t)pfn << PAGE_SHIFT) | _PAGE_TABLE));
}

#endif
