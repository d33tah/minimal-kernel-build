#ifndef _ASM_X86_PGTABLE_32_H
#define _ASM_X86_PGTABLE_32_H

#include <asm/pgtable_32_types.h>

#ifndef __ASSEMBLY__

struct mm_struct;

extern pgd_t swapper_pg_dir[1024];
extern pgd_t initial_page_table[1024];

static inline void native_set_pte(pte_t *ptep, pte_t pte)
{
	*ptep = pte;
}
static inline void native_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}
static inline void native_pte_clear(struct mm_struct *mm, unsigned long addr,
				    pte_t *xp)
{
	*xp = native_make_pte(0);
}
static inline u64 protnone_mask(u64 val)
{
	return 0;
}

#endif

#define PAGE_TABLE_SIZE(pages) ((pages) / PTRS_PER_PGD)

#define LOWMEM_PAGES ((((_ULL(2) << 31) - __PAGE_OFFSET) >> PAGE_SHIFT))

#endif
