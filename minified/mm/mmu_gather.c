#include <linux/mm_types.h>
#include <linux/swap.h>
#include <linux/mm_inline.h>

#include <asm/tlb.h>

void tlb_flush_mmu(struct mmu_gather *tlb)
{
	tlb_flush_mmu_tlbonly(tlb);
}

static void __tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm,
			     bool fullmm)
{
	tlb->mm = mm;
	tlb->fullmm = fullmm;
	tlb->need_flush_all = 0;

	__tlb_reset_range(tlb);
	inc_tlb_flush_pending(tlb->mm);
}

void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm)
{
	__tlb_gather_mmu(tlb, mm, false);
}

void tlb_finish_mmu(struct mmu_gather *tlb)
{
	if (mm_tlb_flush_nested(tlb->mm)) {
		tlb->fullmm = 1;
		__tlb_reset_range(tlb);
		tlb->freed_tables = 1;
	}

	tlb_flush_mmu(tlb);
	dec_tlb_flush_pending(tlb->mm);
}
