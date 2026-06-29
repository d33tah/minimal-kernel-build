#include <linux/kernel.h>
#include <linux/mm_types.h>
#include <linux/swap.h>
#include <linux/mm_inline.h>

#include <asm/tlb.h>

#ifndef CONFIG_MMU_GATHER_NO_GATHER

static void tlb_batch_pages_flush(struct mmu_gather *tlb)
{
	struct mmu_gather_batch *batch;

	for (batch = &tlb->local; batch && batch->nr; batch = batch->next) {
		struct page **pages = batch->pages;

		do {
			 
			unsigned int nr = min(512U, batch->nr);

			free_pages_and_swap_cache(pages, nr);
			pages += nr;
			batch->nr -= nr;

			cond_resched();
		} while (batch->nr);
	}
	tlb->active = &tlb->local;
}

static void tlb_batch_list_free(struct mmu_gather *tlb)
{
	struct mmu_gather_batch *batch, *next;

	for (batch = tlb->local.next; batch; batch = next) {
		next = batch->next;
		free_pages((unsigned long)batch, 0);
	}
	tlb->local.next = NULL;
}

bool __tlb_remove_page_size(struct mmu_gather *tlb, struct page *page, int page_size)
{
	/* runtime-dead: page-table teardown (pte_free_tlb<-free_pgd_range) never
	 * fires on a 1-shot boot that never unmaps; the page-batching gather path
	 * is never entered. Stubbed; symbol kept for the asm/tlb.h inline caller. */
	return false;
}

#endif  


static void tlb_flush_mmu_free(struct mmu_gather *tlb)
{
#ifndef CONFIG_MMU_GATHER_NO_GATHER
	tlb_batch_pages_flush(tlb);
#endif
}

void tlb_flush_mmu(struct mmu_gather *tlb)
{
	tlb_flush_mmu_tlbonly(tlb);
	tlb_flush_mmu_free(tlb);
}

static void __tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm,
			     bool fullmm)
{
	tlb->mm = mm;
	tlb->fullmm = fullmm;

#ifndef CONFIG_MMU_GATHER_NO_GATHER
	tlb->need_flush_all = 0;
	tlb->local.next = NULL;
	tlb->local.nr   = 0;
	tlb->local.max  = ARRAY_SIZE(tlb->__pages);
	tlb->active     = &tlb->local;
	tlb->batch_count = 0;
#endif

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

#ifndef CONFIG_MMU_GATHER_NO_GATHER
	tlb_batch_list_free(tlb);
#endif
	dec_tlb_flush_pending(tlb->mm);
}
