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

#ifndef CONFIG_MMU_GATHER_NO_GATHER

static bool tlb_next_batch(struct mmu_gather *tlb)
{
	struct mmu_gather_batch *batch;

	batch = tlb->active;
	if (batch->next) {
		tlb->active = batch->next;
		return true;
	}

	if (tlb->batch_count == MAX_GATHER_BATCH_COUNT)
		return false;

	batch = (void *)__get_free_pages(GFP_NOWAIT | __GFP_NOWARN, 0);
	if (!batch)
		return false;

	tlb->batch_count++;
	batch->next = NULL;
	batch->nr   = 0;
	batch->max  = MAX_GATHER_BATCH;

	tlb->active->next = batch;
	tlb->active = batch;

	return true;
}

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
	struct mmu_gather_batch *batch;

	VM_BUG_ON(!tlb->end);


	batch = tlb->active;
	 
	batch->pages[batch->nr++] = page;
	if (batch->nr == batch->max) {
		if (!tlb_next_batch(tlb))
			return true;
		batch = tlb->active;
	}
	VM_BUG_ON_PAGE(batch->nr > batch->max, page);

	return false;
}

#endif  


static inline void tlb_table_flush(struct mmu_gather *tlb) { }
static inline void tlb_table_init(struct mmu_gather *tlb) { }


static void tlb_flush_mmu_free(struct mmu_gather *tlb)
{
	tlb_table_flush(tlb);
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

	tlb_table_init(tlb);

	__tlb_reset_range(tlb);
	inc_tlb_flush_pending(tlb->mm);
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
