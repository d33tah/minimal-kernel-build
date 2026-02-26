
#include <asm/tlb.h>

bool __tlb_remove_page_size(struct mmu_gather *tlb, struct page *page,
			    int page_size)
{
	struct mmu_gather_batch *batch;
	batch = tlb->active;
	batch->pages[batch->nr++] = page;
	if (batch->nr == batch->max)
		return true;
	return false;
}

void tlb_flush_mmu(struct mmu_gather *tlb)
{
	tlb_flush_mmu_tlbonly(tlb);

	if (tlb->local.nr) {
		free_pages_and_swap_cache(tlb->local.pages, tlb->local.nr);
		tlb->local.nr = 0;
	}
	tlb->active = &tlb->local;
}

static void __tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm,
			     bool fullmm)
{
	tlb->mm = mm;
	tlb->fullmm = fullmm;
	tlb->need_flush_all = 0;
	tlb->local.next = NULL;
	tlb->local.nr = 0;
	tlb->local.max = ARRAY_SIZE(tlb->__pages);
	tlb->active = &tlb->local;
	tlb->batch_count = 0;
	__tlb_reset_range(tlb);
	atomic_inc(&tlb->mm->tlb_flush_pending);
}

void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm)
{
	__tlb_gather_mmu(tlb, mm, false);
}

void tlb_gather_mmu_fullmm(struct mmu_gather *tlb, struct mm_struct *mm)
{
	__tlb_gather_mmu(tlb, mm, true);
}

void tlb_finish_mmu(struct mmu_gather *tlb)
{
	tlb_flush_mmu(tlb);
	atomic_dec(&tlb->mm->tlb_flush_pending);
}
