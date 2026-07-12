

#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/pagevec.h>
#include <linux/mm_inline.h>

#include "internal.h"


struct lru_pvecs { local_lock_t lock; struct pagevec lru_add; };
static DEFINE_PER_CPU(struct lru_pvecs, lru_pvecs) = { .lock = INIT_LOCAL_LOCK(lock), };

void __put_page(struct page *page) {
	/* runtime-dead: nothing is ever freed on this single-shot boot */
}

static bool pagevec_add_and_need_flush(struct pagevec *pvec, struct page *page) {
	bool ret = false;

	if (!pagevec_add(pvec, page) || PageCompound(page) || lru_cache_disabled())
		ret = true;

	return ret; }

static void __folio_activate(struct folio *folio, struct lruvec *lruvec) {
	if (!folio_test_active(folio)) {
		lruvec_del_folio(lruvec, folio);
		folio_set_active(folio);
		lruvec_add_folio(lruvec, folio); } }

static void folio_activate(struct folio *folio) {
	struct lruvec *lruvec;

	if (folio_test_clear_lru(folio)) {
		lruvec = folio_lruvec_lock_irq(folio);
		__folio_activate(folio, lruvec);
		unlock_page_lruvec_irq(lruvec);
		folio_set_lru(folio); } }

static void __lru_cache_activate_folio(struct folio *folio) {
	struct pagevec *pvec;
	int i;

	local_lock(&lru_pvecs.lock);
	pvec = this_cpu_ptr(&lru_pvecs.lru_add);

	 
	for (i = pagevec_count(pvec) - 1; i >= 0; i--) {
		struct page *pagevec_page = pvec->pages[i];

		if (pagevec_page == &folio->page) {
			folio_set_active(folio);
			break; } }

	local_unlock(&lru_pvecs.lock); }

void folio_mark_accessed(struct folio *folio) {
	if (!folio_test_referenced(folio)) {
		folio_set_referenced(folio);
	} else if (!folio_test_active(folio)) {
		 
		if (folio_test_lru(folio))
			folio_activate(folio);
		else
			__lru_cache_activate_folio(folio);
		folio_clear_referenced(folio); } }

void folio_add_lru(struct folio *folio) {
	struct pagevec *pvec;

	VM_BUG_ON_FOLIO(folio_test_lru(folio), folio);

	folio_get(folio);
	local_lock(&lru_pvecs.lock);
	pvec = this_cpu_ptr(&lru_pvecs.lru_add);
	if (pagevec_add_and_need_flush(pvec, &folio->page))
		__pagevec_lru_add(pvec);
	local_unlock(&lru_pvecs.lock); }

void lru_cache_add_inactive_or_unevictable(struct page *page, struct vm_area_struct *vma) {
	VM_BUG_ON_PAGE(PageLRU(page), page);

	if (!unlikely((vma->vm_flags & (VM_LOCKED | VM_SPECIAL)) == VM_LOCKED))
		lru_cache_add(page); }

void lru_add_drain(void) {
	struct pagevec *pvec;

	local_lock(&lru_pvecs.lock);
	/* folded sole caller of lru_add_drain_cpu() */
	pvec = &per_cpu(lru_pvecs.lru_add, smp_processor_id());
	if (pagevec_count(pvec))
		__pagevec_lru_add(pvec);
	local_unlock(&lru_pvecs.lock); }

atomic_t lru_disable_count = ATOMIC_INIT(0);

void release_pages(struct page **pages, int nr) {
	/* runtime-dead: page-free batch (munmap/mmu_gather) never fires here */
}

void __pagevec_lru_add(struct pagevec *pvec) {
	/* runtime-dead: lru_add pagevec never fills on this single-shot boot */
}


