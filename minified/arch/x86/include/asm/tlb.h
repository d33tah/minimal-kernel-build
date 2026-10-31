/* --- 2025-12-07 10:45 --- Inlined asm-generic/tlb.h content */
#ifndef _ASM_X86_TLB_H
#define _ASM_X86_TLB_H

#include <linux/swap.h>
#include <asm/tlbflush.h>

struct mmu_gather {
	struct mm_struct *mm;

	unsigned long start, end;

	unsigned int fullmm : 1;
	unsigned int need_flush_all : 1;
	unsigned int freed_tables : 1;
	unsigned int cleared_ptes : 1;
	unsigned int cleared_pmds : 1;
	unsigned int cleared_puds : 1;
	unsigned int cleared_p4ds : 1;
};

/* tlb_update_vma_flags removed - 0-caller once tlb_start_vma (its sole
 * caller) was dropped; wrote only the never-read vma_huge/vma_exec/vma_pfn
 * mmu_gather bitfields, which are now removed too (0 reads/writes tree-wide) */

/* tlb_get_unmap_size removed - unused */

/* tlb_remove_page_size + tlb_remove_page + tlb_change_page_size removed -
 * 0-caller tlb-gather user wrappers */

/* tlb_start_vma removed - 0-caller tlb-gather VMA-iteration wrapper; its
 * exclusive callee tlb_update_vma_flags dropped too; flush_cache_range stays
 * as a 0-caller #ifndef-guarded empty stub (asm/cacheflush.h) */

/* tlb_end_vma removed - 0-caller (callee tlb_flush_mmu_tlbonly stays live via mmu_gather.c) */

/* tlb_flush_pud_range / tlb_flush_p4d_range removed - only the dead
 * pud_free_tlb/p4d_free_tlb macros (folded away on 2-level paging) used them */

/* pmd_free_tlb / pud_free_tlb / p4d_free_tlb removed - never invoked on
 * 2-level paging */
/* pte_needs_flush / huge_pmd_needs_flush fallback stubs removed - zero callers tree-wide */

#endif /* _ASM_X86_TLB_H */
