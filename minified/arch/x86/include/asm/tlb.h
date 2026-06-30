/* --- 2025-12-07 10:45 --- Inlined asm-generic/tlb.h content */
#ifndef _ASM_X86_TLB_H
#define _ASM_X86_TLB_H

#include <linux/swap.h>
#include <linux/pagemap.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

#ifndef nmi_uaccess_okay
# define nmi_uaccess_okay() true
#endif

#ifdef tlb_needs_table_invalidate
#error tlb_needs_table_invalidate() requires MMU_GATHER_RCU_TABLE_FREE
#endif

#ifndef CONFIG_MMU_GATHER_NO_GATHER
#define MMU_GATHER_BUNDLE	8

struct mmu_gather_batch {
	struct mmu_gather_batch	*next;
	unsigned int		nr;
	unsigned int		max;
	struct page		*pages[];
};

extern bool __tlb_remove_page_size(struct mmu_gather *tlb, struct page *page,
				   int page_size);
#endif

struct mmu_gather {
	struct mm_struct	*mm;

	unsigned long		start;
	unsigned long		end;

	unsigned int		fullmm : 1;
	unsigned int		need_flush_all : 1;
	unsigned int		freed_tables : 1;
	unsigned int		cleared_ptes : 1;
	unsigned int		cleared_pmds : 1;
	unsigned int		cleared_puds : 1;
	unsigned int		cleared_p4ds : 1;
	unsigned int		vma_exec : 1;
	unsigned int		vma_huge : 1;
	unsigned int		vma_pfn  : 1;

	unsigned int		batch_count;

#ifndef CONFIG_MMU_GATHER_NO_GATHER
	struct mmu_gather_batch *active;
	struct mmu_gather_batch	local;
	struct page		*__pages[MMU_GATHER_BUNDLE];
#endif
};

void tlb_flush_mmu(struct mmu_gather *tlb);

static inline void __tlb_adjust_range(struct mmu_gather *tlb,
				      unsigned long address,
				      unsigned int range_size)
{
	tlb->start = min(tlb->start, address);
	tlb->end = max(tlb->end, address + range_size);
}

static inline void __tlb_reset_range(struct mmu_gather *tlb)
{
	if (tlb->fullmm) {
		tlb->start = tlb->end = ~0;
	} else {
		tlb->start = TASK_SIZE;
		tlb->end = 0;
	}
	tlb->freed_tables = 0;
	tlb->cleared_ptes = 0;
	tlb->cleared_pmds = 0;
	tlb->cleared_puds = 0;
	tlb->cleared_p4ds = 0;
}

static inline void
tlb_update_vma_flags(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	tlb->vma_huge = 0;
	tlb->vma_exec = !!(vma->vm_flags & VM_EXEC);
	tlb->vma_pfn  = !!(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP));
}

static inline unsigned long tlb_get_unmap_shift(struct mmu_gather *tlb)
{
	if (tlb->cleared_ptes)
		return PAGE_SHIFT;
	if (tlb->cleared_pmds)
		return PMD_SHIFT;
	if (tlb->cleared_puds)
		return PUD_SHIFT;
	if (tlb->cleared_p4ds)
		return P4D_SHIFT;

	return PAGE_SHIFT;
}

/* tlb_get_unmap_size removed - unused */

/* X86-specific tlb_flush - defined before generic version would be */
#define tlb_flush tlb_flush
static inline void tlb_flush(struct mmu_gather *tlb)
{
	unsigned long start = 0UL, end = TLB_FLUSH_ALL;
	unsigned int stride_shift = tlb_get_unmap_shift(tlb);

	if (!tlb->fullmm && !tlb->need_flush_all) {
		start = tlb->start;
		end = tlb->end;
	}

	flush_tlb_mm_range(tlb->mm, start, end, stride_shift, tlb->freed_tables);
}

static inline void tlb_flush_mmu_tlbonly(struct mmu_gather *tlb)
{
	if (!(tlb->freed_tables || tlb->cleared_ptes || tlb->cleared_pmds ||
	      tlb->cleared_puds || tlb->cleared_p4ds))
		return;

	tlb_flush(tlb);
	__tlb_reset_range(tlb);
}

/* tlb_remove_page_size + tlb_remove_page + tlb_change_page_size removed -
 * 0-caller tlb-gather user wrappers (externs __tlb_remove_page_size/tlb_flush_mmu stay live) */

static inline void tlb_start_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (tlb->fullmm)
		return;

	tlb_update_vma_flags(tlb, vma);
#ifndef CONFIG_MMU_GATHER_NO_FLUSH_CACHE
	flush_cache_range(vma, vma->vm_start, vma->vm_end);
#endif
}

/* tlb_end_vma removed - 0-caller (callee tlb_flush_mmu_tlbonly stays live via mmu_gather.c) */

static inline void tlb_flush_pmd_range(struct mmu_gather *tlb,
				     unsigned long address, unsigned long size)
{
	__tlb_adjust_range(tlb, address, size);
	tlb->cleared_pmds = 1;
}

/* tlb_flush_pud_range / tlb_flush_p4d_range removed - only the dead
 * pud_free_tlb/p4d_free_tlb macros (folded away on 2-level paging) used them */

#ifndef pte_free_tlb
#define pte_free_tlb(tlb, ptep, address)			\
	do {							\
		tlb_flush_pmd_range(tlb, address, PAGE_SIZE);	\
		tlb->freed_tables = 1;				\
		__pte_free_tlb(tlb, ptep, address);		\
	} while (0)
#endif

/* pmd_free_tlb / pud_free_tlb / p4d_free_tlb removed - never invoked on
 * 2-level paging (mm/memory.c free_pgd_range only calls pte_free_tlb) */
/* pte_needs_flush / huge_pmd_needs_flush fallback stubs removed - zero callers tree-wide */


#endif /* _ASM_X86_TLB_H */
