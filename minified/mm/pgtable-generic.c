
#include <linux/pagemap.h>
#include <linux/pgtable.h>
#include <linux/mm_inline.h>
#include <asm/tlb.h>

void pmd_clear_bad(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_clear(pmd);
}
/* ptep_clear_flush removed - never called */
