#include <linux/mm_types.h>
#include <linux/swap.h>
#include <linux/mm_inline.h>

#include <asm/tlb.h>

static void __tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm,
			     bool fullmm)
{
	tlb->mm = mm;
	tlb->fullmm = fullmm;
	tlb->need_flush_all = 0;

	__tlb_reset_range(tlb);
	inc_tlb_flush_pending(tlb->mm);
}
