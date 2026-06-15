
#include <linux/pagemap.h>
#include <linux/hugetlb.h>
#include <linux/pgtable.h>
#include <linux/mm_inline.h>
#include <asm/tlb.h>


void pgd_clear_bad(pgd_t *pgd)
{
	pgd_ERROR(*pgd);
	pgd_clear(pgd);
}

/*
 * p4d_clear_bad / pud_clear_bad were here. Both page-table levels are folded
 * on this build, so __PAGETABLE_{P4D,PUD}_FOLDED are defined and pgtable.h
 * #defines p4d_clear_bad/pud_clear_bad to do {} while (0). The out-of-line
 * versions were guarded by #ifndef __PAGETABLE_*_FOLDED and never compiled --
 * removed as dead code.
 */

void pmd_clear_bad(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_clear(pmd);
}

/*
 * ptep_set_access_flags / ptep_clear_flush_young were here, both guarded by
 * #ifndef __HAVE_ARCH_PTEP_{SET_ACCESS_FLAGS,CLEAR_YOUNG_FLUSH}. x86 defines
 * both arch overrides (asm/pgtable.h), so the generic out-of-line bodies are
 * never compiled -- removed as dead code.
 */
