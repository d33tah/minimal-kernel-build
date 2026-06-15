#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/nmi.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>

#include <asm/cpu_entry_area.h>
#include <asm/fixmap.h>
#include <asm/e820/api.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <linux/vmalloc.h>

unsigned int __VMALLOC_RESERVE = 128 << 20;

  
void set_pte_vaddr(unsigned long vaddr, pte_t pteval)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	pgd = swapper_pg_dir + pgd_index(vaddr);
	if (pgd_none(*pgd)) {
		BUG();
		return;
	}
	/*
	 * P4D/PUD are folded onto the PGD on this 2-level (X86_32, no PAE)
	 * build, so p4d_offset()/pud_offset() are identity casts and
	 * p4d_none()/pud_none() are constant 0 -- descend straight to the PMD.
	 */
	pmd = pmd_offset(pud_offset(p4d_offset(pgd, vaddr), vaddr), vaddr);
	if (pmd_none(*pmd)) {
		BUG();
		return;
	}
	pte = pte_offset_kernel(pmd, vaddr);
	if (!pte_none(pteval))
		set_pte_at(&init_mm, vaddr, pte, pteval);
	else
		pte_clear(&init_mm, vaddr, pte);

	 
	flush_tlb_one_kernel(vaddr);
}

unsigned long __FIXADDR_TOP = 0xfffff000;

