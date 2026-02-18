
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/mman.h>
#ifndef _LINUX_PAGEVEC_H
#define _LINUX_PAGEVEC_H
#include <linux/xarray.h>
#define PAGEVEC_SIZE 15
struct page;
struct folio;
struct address_space;
struct pagevec {
	unsigned char nr;
	bool percpu_pvec_drained;
	struct page *pages[PAGEVEC_SIZE];
};
void __pagevec_lru_add(struct pagevec *pvec);
static inline void pagevec_reinit(struct pagevec *pvec)
{
	pvec->nr = 0;
}
static inline unsigned pagevec_count(struct pagevec *pvec)
{
	return pvec->nr;
}
static inline unsigned pagevec_add(struct pagevec *pvec, struct page *page)
{
	pvec->pages[pvec->nr++] = page;
	return PAGEVEC_SIZE - pvec->nr;
}
#endif
#include <linux/mm_inline.h>
#include <linux/percpu_counter.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/backing-dev.h>
#include <linux/gfp.h>
#include <linux/local_lock.h>

struct lru_pvecs {
	local_lock_t lock;
	struct pagevec lru_add;
};
static DEFINE_PER_CPU(struct lru_pvecs, lru_pvecs) = {
	.lock = INIT_LOCAL_LOCK(lock),
};

static void __page_cache_release(struct page *page)
{
	if (PageLRU(page)) {
		struct folio *folio = page_folio(page);
		struct lruvec *lruvec;
		unsigned long flags;

		lruvec = folio_lruvec_lock_irqsave(folio, &flags);
		del_page_from_lru_list(page, lruvec);
		__clear_page_lru_flags(page);
		unlock_page_lruvec_irqrestore(lruvec, flags);
	}

	if (unlikely(PageMlocked(page))) {
		VM_BUG_ON_PGFLAGS(PageTail(page), page);
		int nr_pages = compound_nr(page);

		__ClearPageMlocked(page);
		mod_zone_page_state(page_zone(page), NR_MLOCK, -nr_pages);
	}
}

void __put_page(struct page *page)
{
	__page_cache_release(page);
	if (unlikely(PageCompound(page))) {
		destroy_compound_page(page);
	}
}

void folio_add_lru(struct folio *folio)
{
	struct pagevec *pvec;
	folio_get(folio);
	local_lock(&lru_pvecs.lock);
	pvec = this_cpu_ptr(&lru_pvecs.lru_add);
	if (!pagevec_add(pvec, &folio->page) || PageCompound(&folio->page))
		__pagevec_lru_add(pvec);
	local_unlock(&lru_pvecs.lock);
}

void lru_cache_add_inactive_or_unevictable(struct page *page,
					   struct vm_area_struct *vma)
{
	folio_add_lru(page_folio(page));
}

void lru_add_drain_cpu(int cpu)
{
	struct pagevec *pvec = &per_cpu(lru_pvecs.lru_add, cpu);

	if (pagevec_count(pvec))
		__pagevec_lru_add(pvec);
}

void lru_add_drain(void)
{
	local_lock(&lru_pvecs.lock);
	lru_add_drain_cpu(smp_processor_id());
	local_unlock(&lru_pvecs.lock);
}

void release_pages(struct page **pages, int nr)
{
	int i;
	LIST_HEAD(pages_to_free);
	struct lruvec *lruvec = NULL;
	unsigned long flags = 0;
	unsigned int lock_batch;

	for (i = 0; i < nr; i++) {
		struct page *page = pages[i];
		struct folio *folio = page_folio(page);

		if (lruvec && ++lock_batch == SWAP_CLUSTER_MAX) {
			unlock_page_lruvec_irqrestore(lruvec, flags);
			lruvec = NULL;
		}

		page = &folio->page;
		if (!put_page_testzero(page))
			continue;

		if (PageCompound(page)) {
			if (lruvec) {
				unlock_page_lruvec_irqrestore(lruvec, flags);
				lruvec = NULL;
			}
			__page_cache_release(page);
			destroy_compound_page(page);
			continue;
		}

		if (PageLRU(page)) {
			struct lruvec *prev_lruvec = lruvec;

			lruvec = folio_lruvec_relock_irqsave(folio, lruvec,
							     &flags);
			if (prev_lruvec != lruvec)
				lock_batch = 0;

			del_page_from_lru_list(page, lruvec);
			__clear_page_lru_flags(page);
		}

		if (unlikely(PageMlocked(page))) {
			__ClearPageMlocked(page);
			dec_zone_page_state(page, NR_MLOCK);
		}

		list_add(&page->lru, &pages_to_free);
	}
	if (lruvec)
		unlock_page_lruvec_irqrestore(lruvec, flags);
}

void __pagevec_lru_add(struct pagevec *pvec)
{
	int i;
	struct lruvec *lruvec = NULL;
	unsigned long flags = 0;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct folio *folio = page_folio(pvec->pages[i]);
		lruvec = folio_lruvec_relock_irqsave(folio, lruvec, &flags);
		folio_set_lru(folio);
		lruvec_add_folio(lruvec, folio);
	}
	if (lruvec)
		unlock_page_lruvec_irqrestore(lruvec, flags);
	release_pages(pvec->pages, pvec->nr);
	pagevec_reinit(pvec);
}
