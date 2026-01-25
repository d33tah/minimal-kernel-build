#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/mmdebug.h>
#include <linux/mm_types.h>
#include <linux/mm_inline.h>
#include <linux/pagemap.h>
#include <linux/rcupdate.h>
#include <linux/smp.h>
#include <linux/swap.h>

#include <asm/pgalloc.h>
#include <asm/tlb.h>

bool __tlb_remove_page_size(struct mmu_gather *tlb, struct page *page,
			    int page_size)
{
	struct mmu_gather_batch *batch;
	batch = tlb->active;
	batch->pages[batch->nr++] = page;
	if (batch->nr == batch->max) {
		batch = tlb->active;
		if (batch->next) {
			tlb->active = batch->next;
		} else if (tlb->batch_count == MAX_GATHER_BATCH_COUNT) {
			return true;
		} else {
			batch = (void *)__get_free_pages(
				GFP_NOWAIT | __GFP_NOWARN, 0);
			if (!batch)
				return true;
			tlb->batch_count++;
			batch->next = NULL;
			batch->nr = 0;
			batch->max = MAX_GATHER_BATCH;
			tlb->active->next = batch;
			tlb->active = batch;
		}
		batch = tlb->active;
	}
	return false;
}

void tlb_flush_mmu(struct mmu_gather *tlb)
{
	struct mmu_gather_batch *batch;

	tlb_flush_mmu_tlbonly(tlb);

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
	/* tlb_table_init removed - empty stub */

	__tlb_reset_range(tlb);
	atomic_inc(
		&tlb->mm->tlb_flush_pending); /* inc_tlb_flush_pending inlined */
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
	struct mmu_gather_batch *batch, *next;

	if (atomic_read(&tlb->mm->tlb_flush_pending) >
	    1) { /* mm_tlb_flush_nested inlined */
		tlb->fullmm = 1;
		__tlb_reset_range(tlb);
		tlb->freed_tables = 1;
	}

	tlb_flush_mmu(tlb);

	for (batch = tlb->local.next; batch; batch = next) {
		next = batch->next;
		/* free_pages removed - empty stub */
	}
	tlb->local.next = NULL;
	atomic_dec(
		&tlb->mm->tlb_flush_pending); /* dec_tlb_flush_pending inlined */
}
