 
#ifndef _ASM_X86_PGTABLE_32_H
#define _ASM_X86_PGTABLE_32_H

#include <asm/pgtable_32_types.h>

 
#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <linux/threads.h>
/* paravirt.h removed - header is empty */
#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct mm_struct;
struct vm_area_struct;

extern pgd_t swapper_pg_dir[1024];
extern pgd_t initial_page_table[1024];
extern pmd_t initial_pg_pmd[];

void paging_init(void);
void sync_initial_page_table(void);

/* --- 2025-12-07 20:18 --- Inlined pgtable-2level.h */
/* pte_ERROR removed - unused */
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
static inline void native_set_pud(pud_t *pudp, pud_t pud)
{
}
/* native_set_pte_atomic removed - never called */
static inline void native_pmd_clear(pmd_t *pmdp)
{
	native_set_pmd(pmdp, __pmd(0));
}
static inline void native_pud_clear(pud_t *pudp)
{
}
static inline void native_pte_clear(struct mm_struct *mm,
				    unsigned long addr, pte_t *xp)
{
	*xp = native_make_pte(0);
}
#define native_ptep_get_and_clear(xp) native_local_ptep_get_and_clear(xp)
#define native_pmdp_get_and_clear(xp) native_local_pmdp_get_and_clear(xp)
#define native_pudp_get_and_clear(xp) native_local_pudp_get_and_clear(xp)
/* Swap/protnone/invert macros removed - unused */
/* kpte_clear_flush removed - unused */

#endif

/* kern_addr_valid removed - unused */

#if PTRS_PER_PMD > 1
#define PAGE_TABLE_SIZE(pages) (((pages) / PTRS_PER_PMD) + PTRS_PER_PGD)
#else
#define PAGE_TABLE_SIZE(pages) ((pages) / PTRS_PER_PGD)
#endif

 
/* Reduced for minimal 2MB boot - original: ((((_ULL(2)<<31) - __PAGE_OFFSET) >> PAGE_SHIFT)) */
#define LOWMEM_PAGES (2 * 1024 * 1024 / 4096)  /* 2MB / 4KB = 512 pages */

#endif  
