 
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/hugetlb.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/fixmap.h>
#include <asm/mtrr.h>


#define PGTABLE_HIGHMEM 0

static inline
void paravirt_tlb_remove_table(struct mmu_gather *tlb, void *table)
{
	tlb_remove_page(tlb, table);
}

gfp_t __userpte_alloc_gfp = GFP_PGTABLE_USER | PGTABLE_HIGHMEM;

pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	return __pte_alloc_one(mm, __userpte_alloc_gfp);
}

/* Stub: userpte= cmdline option not needed for minimal kernel */
static int __init setup_userpte(char *arg) { return 0; }
early_param("userpte", setup_userpte);

void ___pte_free_tlb(struct mmu_gather *tlb, struct page *pte)
{
	pgtable_pte_page_dtor(pte);
	paravirt_release_pte(page_to_pfn(pte));
	paravirt_tlb_remove_table(tlb, pte);
}

#if CONFIG_PGTABLE_LEVELS > 2
void ___pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd)
{
	struct page *page = virt_to_page(pmd);
	paravirt_release_pmd(__pa(pmd) >> PAGE_SHIFT);
	 
	pgtable_pmd_page_dtor(page);
	paravirt_tlb_remove_table(tlb, page);
}

#if CONFIG_PGTABLE_LEVELS > 3
void ___pud_free_tlb(struct mmu_gather *tlb, pud_t *pud)
{
	paravirt_release_pud(__pa(pud) >> PAGE_SHIFT);
	paravirt_tlb_remove_table(tlb, virt_to_page(pud));
}

#if CONFIG_PGTABLE_LEVELS > 4
void ___p4d_free_tlb(struct mmu_gather *tlb, p4d_t *p4d)
{
	paravirt_release_p4d(__pa(p4d) >> PAGE_SHIFT);
	paravirt_tlb_remove_table(tlb, virt_to_page(p4d));
}
#endif	 
#endif	 
#endif	 

static inline void pgd_list_add(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);

	list_add(&page->lru, &pgd_list);
}

static inline void pgd_list_del(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);

	list_del(&page->lru);
}

#define UNSHARED_PTRS_PER_PGD				\
	(SHARED_KERNEL_PMD ? KERNEL_PGD_BOUNDARY : PTRS_PER_PGD)
#define MAX_UNSHARED_PTRS_PER_PGD			\
	max_t(size_t, KERNEL_PGD_BOUNDARY, PTRS_PER_PGD)


static void pgd_set_mm(pgd_t *pgd, struct mm_struct *mm)
{
	virt_to_page(pgd)->pt_mm = mm;
}

struct mm_struct *pgd_page_get_mm(struct page *page)
{
	return page->pt_mm;
}

static void pgd_ctor(struct mm_struct *mm, pgd_t *pgd)
{
	 
	if (CONFIG_PGTABLE_LEVELS == 2 ||
	    (CONFIG_PGTABLE_LEVELS == 3 && SHARED_KERNEL_PMD) ||
	    CONFIG_PGTABLE_LEVELS >= 4) {
		clone_pgd_range(pgd + KERNEL_PGD_BOUNDARY,
				swapper_pg_dir + KERNEL_PGD_BOUNDARY,
				KERNEL_PGD_PTRS);
	}

	 
	if (!SHARED_KERNEL_PMD) {
		pgd_set_mm(pgd, mm);
		pgd_list_add(pgd);
	}
}

static void pgd_dtor(pgd_t *pgd)
{
	if (SHARED_KERNEL_PMD)
		return;

	spin_lock(&pgd_lock);
	pgd_list_del(pgd);
	spin_unlock(&pgd_lock);
}

 


 
#define PREALLOCATED_PMDS	0
#define MAX_PREALLOCATED_PMDS	0
#define PREALLOCATED_USER_PMDS	 0
#define MAX_PREALLOCATED_USER_PMDS 0

static void free_pmds(struct mm_struct *mm, pmd_t *pmds[], int count)
{
	int i;

	for (i = 0; i < count; i++)
		if (pmds[i]) {
			pgtable_pmd_page_dtor(virt_to_page(pmds[i]));
			free_page((unsigned long)pmds[i]);
			mm_dec_nr_pmds(mm);
		}
}

static int preallocate_pmds(struct mm_struct *mm, pmd_t *pmds[], int count)
{
	int i;
	bool failed = false;
	gfp_t gfp = GFP_PGTABLE_USER;

	if (mm == &init_mm)
		gfp &= ~__GFP_ACCOUNT;

	for (i = 0; i < count; i++) {
		pmd_t *pmd = (pmd_t *)__get_free_page(gfp);
		if (!pmd)
			failed = true;
		if (pmd && !pgtable_pmd_page_ctor(virt_to_page(pmd))) {
			free_page((unsigned long)pmd);
			pmd = NULL;
			failed = true;
		}
		if (pmd)
			mm_inc_nr_pmds(mm);
		pmds[i] = pmd;
	}

	if (failed) {
		free_pmds(mm, pmds, count);
		return -ENOMEM;
	}

	return 0;
}

 
static void mop_up_one_pmd(struct mm_struct *mm, pgd_t *pgdp)
{
	pgd_t pgd = *pgdp;

	if (pgd_val(pgd) != 0) {
		pmd_t *pmd = (pmd_t *)pgd_page_vaddr(pgd);

		pgd_clear(pgdp);

		paravirt_release_pmd(pgd_val(pgd) >> PAGE_SHIFT);
		pmd_free(mm, pmd);
		mm_dec_nr_pmds(mm);
	}
}

static void pgd_mop_up_pmds(struct mm_struct *mm, pgd_t *pgdp)
{
	int i;

	for (i = 0; i < PREALLOCATED_PMDS; i++)
		mop_up_one_pmd(mm, &pgdp[i]);

}

static void pgd_prepopulate_pmd(struct mm_struct *mm, pgd_t *pgd, pmd_t *pmds[])
{
	p4d_t *p4d;
	pud_t *pud;
	int i;

	if (PREALLOCATED_PMDS == 0)  
		return;

	p4d = p4d_offset(pgd, 0);
	pud = pud_offset(p4d, 0);

	for (i = 0; i < PREALLOCATED_PMDS; i++, pud++) {
		pmd_t *pmd = pmds[i];

		if (i >= KERNEL_PGD_BOUNDARY)
			memcpy(pmd, (pmd_t *)pgd_page_vaddr(swapper_pg_dir[i]),
			       sizeof(pmd_t) * PTRS_PER_PMD);

		pud_populate(mm, pud, pmd);
	}
}

static void pgd_prepopulate_user_pmd(struct mm_struct *mm,
				     pgd_t *k_pgd, pmd_t *pmds[])
{
}
 

static inline pgd_t *_pgd_alloc(void)
{
	return (pgd_t *)__get_free_pages(GFP_PGTABLE_USER,
					 PGD_ALLOCATION_ORDER);
}

static inline void _pgd_free(pgd_t *pgd)
{
	free_pages((unsigned long)pgd, PGD_ALLOCATION_ORDER);
}

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;
	pmd_t *u_pmds[MAX_PREALLOCATED_USER_PMDS];
	pmd_t *pmds[MAX_PREALLOCATED_PMDS];

	pgd = _pgd_alloc();

	if (pgd == NULL)
		goto out;

	mm->pgd = pgd;

	if (preallocate_pmds(mm, pmds, PREALLOCATED_PMDS) != 0)
		goto out_free_pgd;

	if (preallocate_pmds(mm, u_pmds, PREALLOCATED_USER_PMDS) != 0)
		goto out_free_pmds;

	if (paravirt_pgd_alloc(mm) != 0)
		goto out_free_user_pmds;

	 
	spin_lock(&pgd_lock);

	pgd_ctor(mm, pgd);
	pgd_prepopulate_pmd(mm, pgd, pmds);
	pgd_prepopulate_user_pmd(mm, pgd, u_pmds);

	spin_unlock(&pgd_lock);

	return pgd;

out_free_user_pmds:
	free_pmds(mm, u_pmds, PREALLOCATED_USER_PMDS);
out_free_pmds:
	free_pmds(mm, pmds, PREALLOCATED_PMDS);
out_free_pgd:
	_pgd_free(pgd);
out:
	return NULL;
}

void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	pgd_mop_up_pmds(mm, pgd);
	pgd_dtor(pgd);
	paravirt_pgd_free(mm, pgd);
	_pgd_free(pgd);
}

 
int ptep_set_access_flags(struct vm_area_struct *vma,
			  unsigned long address, pte_t *ptep,
			  pte_t entry, int dirty)
{
	int changed = !pte_same(*ptep, entry);

	if (changed && dirty)
		set_pte(ptep, entry);

	return changed;
}


int ptep_test_and_clear_young(struct vm_area_struct *vma,
			      unsigned long addr, pte_t *ptep)
{
	int ret = 0;

	if (pte_young(*ptep))
		ret = test_and_clear_bit(_PAGE_BIT_ACCESSED,
					 (unsigned long *) &ptep->pte);

	return ret;
}


int ptep_clear_flush_young(struct vm_area_struct *vma,
			   unsigned long address, pte_t *ptep)
{
	 
	return ptep_test_and_clear_young(vma, address, ptep);
}


 
void __init reserve_top_address(unsigned long reserve)
{
	BUG_ON(fixmaps_set > 0);
	__FIXADDR_TOP = round_down(-reserve, 1 << PMD_SHIFT) - PAGE_SIZE;
	printk(KERN_INFO "Reserving virtual address space above 0x%08lx (rounded to 0x%08lx)\n",
	       -reserve, __FIXADDR_TOP + PAGE_SIZE);
}

int fixmaps_set;

void __native_set_fixmap(enum fixed_addresses idx, pte_t pte)
{
	unsigned long address = __fix_to_virt(idx);


	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	set_pte_vaddr(address, pte);
	fixmaps_set++;
}

void native_set_fixmap(unsigned   idx,
		       phys_addr_t phys, pgprot_t flags)
{
	 
	pgprot_val(flags) &= __default_kernel_pte_mask;

	__native_set_fixmap(idx, pfn_pte(phys >> PAGE_SHIFT, flags));
}

