// SPDX-License-Identifier: GPL-2.0
#include <asm/tlbflush.h>

/*
 * Associate a virtual page frame with a given physical page frame 
 * and protection flags for that frame.
 */
void set_pte_vaddr(unsigned long vaddr, pte_t pteval)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	pgd = swapper_pg_dir + pgd_index(vaddr);
	if (pgd_none(*pgd)) {
		BUG();
	}
	pmd = pmd_offset(pud_offset(p4d_offset(pgd, vaddr), vaddr), vaddr);
	if (pmd_none(*pmd)) {
		BUG();
	}
	pte = pte_offset_kernel(pmd, vaddr);
	if (!pte_none(pteval))
		set_pte_at(&init_mm, vaddr, pte, pteval);
	else
		pte_clear(&init_mm, vaddr, pte);

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	flush_tlb_one_kernel(vaddr);
}

unsigned long __FIXADDR_TOP = 0xfffff000;
