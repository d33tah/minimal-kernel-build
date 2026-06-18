
#include <linux/pagemap.h>
#include <linux/hugetlb.h>
#include <linux/pgtable.h>
#include <linux/mm_inline.h>
#include <asm/tlb.h>


/*
 * pgd_clear_bad / p4d_clear_bad / pud_clear_bad were here. p4d/pud are folded
 * on this build (__PAGETABLE_{P4D,PUD}_FOLDED), and pgd_bad() is constant 0 on
 * x86, so pgtable.h #defines all three to do {} while (0) and the out-of-line
 * bodies (some guarded by #ifndef __PAGETABLE_*_FOLDED) are never reached --
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
