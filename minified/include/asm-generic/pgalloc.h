#ifndef __ASM_GENERIC_PGALLOC_H
#define __ASM_GENERIC_PGALLOC_H


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


#endif  
