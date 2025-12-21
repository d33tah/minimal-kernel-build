 
#ifndef _ASM_X86_PGTABLE_32_H
#define _ASM_X86_PGTABLE_32_H

#include <asm/pgtable_32_types.h>

 
#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <linux/threads.h>
#include <asm/paravirt.h>

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
#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %08lx\n", __FILE__, __LINE__, (e).pte_low)
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
static inline void native_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	native_set_pte(ptep, pte);
}
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
#define SWP_TYPE_BITS 5
#define SWP_OFFSET_SHIFT (_PAGE_BIT_PROTNONE + 1)
#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > SWP_TYPE_BITS)
#define __swp_type(x)			(((x).val >> (_PAGE_BIT_PRESENT + 1)) \
					 & ((1U << SWP_TYPE_BITS) - 1))
#define __swp_offset(x)			((x).val >> SWP_OFFSET_SHIFT)
#define __swp_entry(type, offset)	((swp_entry_t) { \
					 ((type) << (_PAGE_BIT_PRESENT + 1)) \
					 | ((offset) << SWP_OFFSET_SHIFT) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { (pte).pte_low })
#define __swp_entry_to_pte(x)		((pte_t) { .pte = (x).val })
static inline u64 protnone_mask(u64 val)
{
	return 0;
}
static inline u64 flip_protnone_guard(u64 oldval, u64 val, u64 mask)
{
	return val;
}
static inline bool __pte_needs_invert(u64 val)
{
	return false;
}

 
#define kpte_clear_flush(ptep, vaddr)		\
do {						\
	pte_clear(&init_mm, (vaddr), (ptep));	\
	flush_tlb_one_kernel((vaddr));		\
} while (0)

#endif  

 
#define kern_addr_valid(addr)	(1)

 
#if PTRS_PER_PMD > 1
#define PAGE_TABLE_SIZE(pages) (((pages) / PTRS_PER_PMD) + PTRS_PER_PGD)
#else
#define PAGE_TABLE_SIZE(pages) ((pages) / PTRS_PER_PGD)
#endif

 
#define LOWMEM_PAGES ((((_ULL(2)<<31) - __PAGE_OFFSET) >> PAGE_SHIFT))

#endif  
