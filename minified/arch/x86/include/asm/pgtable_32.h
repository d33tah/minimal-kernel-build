 
#ifndef _ASM_X86_PGTABLE_32_H
#define _ASM_X86_PGTABLE_32_H

#include <asm/pgtable_32_types.h>

 
#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <linux/threads.h>

#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct mm_struct;
struct vm_area_struct;

extern pgd_t swapper_pg_dir[1024];
extern pgd_t initial_page_table[1024];

void sync_initial_page_table(void);

/* --- 2025-12-07 20:18 --- Inlined pgtable-2level.h */
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx\n", __FILE__, __LINE__, pgd_val(e))

static inline void native_set_pte(pte_t *ptep , pte_t pte)
{
	*ptep = pte;
}
static inline void native_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}
/* native_set_pud removed - referenced only from the dead #ifndef set_pud block */
static inline void native_pmd_clear(pmd_t *pmdp)
{
	native_set_pmd(pmdp, __pmd(0));
}
static inline void native_pte_clear(struct mm_struct *mm,
				    unsigned long addr, pte_t *xp)
{
	*xp = native_make_pte(0);
}
static inline u64 protnone_mask(u64 val)
{
	return 0;
}

#endif  


#if PTRS_PER_PMD > 1
#define PAGE_TABLE_SIZE(pages) (((pages) / PTRS_PER_PMD) + PTRS_PER_PGD)
#else
#define PAGE_TABLE_SIZE(pages) ((pages) / PTRS_PER_PGD)
#endif

 
#define LOWMEM_PAGES ((((_ULL(2)<<31) - __PAGE_OFFSET) >> PAGE_SHIFT))

#endif  
