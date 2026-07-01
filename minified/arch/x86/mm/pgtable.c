#include <linux/mm.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/fixmap.h>


#define PGTABLE_HIGHMEM 0

gfp_t __userpte_alloc_gfp = GFP_PGTABLE_USER | PGTABLE_HIGHMEM;

pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	return __pte_alloc_one(mm, __userpte_alloc_gfp);
}


void ___pte_free_tlb(struct mmu_gather *tlb, struct page *pte)
{
	/* runtime-dead: reached only via pte_free_tlb<-free_pgd_range (page-table
	 * teardown on munmap/exit), which never fires on a 1-shot boot. Stubbed;
	 * symbol kept for the asm/pgalloc.h __pte_free_tlb inline caller. */
}

static inline void pgd_list_add(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);

	list_add(&page->lru, &pgd_list);
}

static void pgd_set_mm(pgd_t *pgd, struct mm_struct *mm)
{
	virt_to_page(pgd)->pt_mm = mm;
}

static void pgd_ctor(struct mm_struct *mm, pgd_t *pgd)
{
	clone_pgd_range(pgd + KERNEL_PGD_BOUNDARY,
			swapper_pg_dir + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

	pgd_set_mm(pgd, mm);
	pgd_list_add(pgd);
}

static inline pgd_t *_pgd_alloc(void)
{
	return (pgd_t *)__get_free_pages(GFP_PGTABLE_USER,
					 PGD_ALLOCATION_ORDER);
}

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;

	pgd = _pgd_alloc();

	if (pgd == NULL)
		return NULL;

	mm->pgd = pgd;

	spin_lock(&pgd_lock);

	pgd_ctor(mm, pgd);

	spin_unlock(&pgd_lock);

	return pgd;
}

void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	/* runtime-dead: reached only via mm_free_pgd<-__mmdrop (mm teardown at
	 * refcount 0 / munmap-exit), which never fires on a 1-shot boot whose
	 * init mm is never dropped. Stubbed; symbol kept for the asm/pgalloc.h
	 * extern + fork.c mm_free_pgd caller. Cascaded away: pgd_dtor,
	 * pgd_list_del, _pgd_free (all private to this teardown root). */
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


/*
 * ptep_test_and_clear_young / ptep_clear_flush_young removed - no callers
 * (the generic rmap/aging paths that referenced them are minified out).
 */



int fixmaps_set;

void native_set_fixmap(unsigned   idx,
		       phys_addr_t phys, pgprot_t flags)
{
	unsigned long address = __fix_to_virt(idx);

	pgprot_val(flags) &= __default_kernel_pte_mask;

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	set_pte_vaddr(address, pfn_pte(phys >> PAGE_SHIFT, flags));
	fixmaps_set++;
}

