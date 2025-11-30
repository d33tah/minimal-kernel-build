 
 

 

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/tracepoint.h>
#include <linux/mm_inline.h>
#include <linux/percpu_counter.h>
#include <linux/memremap.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/backing-dev.h>
#include <linux/memcontrol.h>
#include <linux/gfp.h>
#include <linux/uio.h>
#include <linux/hugetlb.h>
#include <linux/page_idle.h>
#include <linux/local_lock.h>
#include <linux/buffer_head.h>

#include "internal.h"


 
int page_cluster;

 
struct lru_rotate {
	local_lock_t lock;
	struct pagevec pvec;
};
static DEFINE_PER_CPU(struct lru_rotate, lru_rotate) = {
	.lock = INIT_LOCAL_LOCK(lock),
};

 
struct lru_pvecs {
	local_lock_t lock;
	struct pagevec lru_add;
	struct pagevec lru_deactivate_file;
	struct pagevec lru_deactivate;
	struct pagevec lru_lazyfree;
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
		int nr_pages = thp_nr_pages(page);

		__ClearPageMlocked(page);
		mod_zone_page_state(page_zone(page), NR_MLOCK, -nr_pages);
		count_vm_events(UNEVICTABLE_PGCLEARED, nr_pages);
	}
}

static void __put_single_page(struct page *page)
{
	__page_cache_release(page);
	mem_cgroup_uncharge(page_folio(page));
	free_unref_page(page, 0);
}

static void __put_compound_page(struct page *page)
{
	 
	if (!PageHuge(page))
		__page_cache_release(page);
	destroy_compound_page(page);
}

void __put_page(struct page *page)
{
	if (unlikely(is_zone_device_page(page)))
		free_zone_device_page(page);
	else if (unlikely(PageCompound(page)))
		__put_compound_page(page);
	else
		__put_single_page(page);
}

 
/* Stub: put_pages_list not used in minimal kernel */
void put_pages_list(struct list_head *pages)
{
	INIT_LIST_HEAD(pages);
}

 
int get_kernel_pages(const struct kvec *kiov, int nr_segs, int write,
		struct page **pages)
{
	return 0;
}

static void pagevec_lru_move_fn(struct pagevec *pvec,
	void (*move_fn)(struct page *page, struct lruvec *lruvec))
{
	int i;
	struct lruvec *lruvec = NULL;
	unsigned long flags = 0;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];
		struct folio *folio = page_folio(page);

		 
		if (!TestClearPageLRU(page))
			continue;

		lruvec = folio_lruvec_relock_irqsave(folio, lruvec, &flags);
		(*move_fn)(page, lruvec);

		SetPageLRU(page);
	}
	if (lruvec)
		unlock_page_lruvec_irqrestore(lruvec, flags);
	release_pages(pvec->pages, pvec->nr);
	pagevec_reinit(pvec);
}

static void pagevec_move_tail_fn(struct page *page, struct lruvec *lruvec)
{
	struct folio *folio = page_folio(page);

	if (!folio_test_unevictable(folio)) {
		lruvec_del_folio(lruvec, folio);
		folio_clear_active(folio);
		lruvec_add_folio_tail(lruvec, folio);
		__count_vm_events(PGROTATED, folio_nr_pages(folio));
	}
}

 
static bool pagevec_add_and_need_flush(struct pagevec *pvec, struct page *page)
{
	bool ret = false;

	if (!pagevec_add(pvec, page) || PageCompound(page) ||
			lru_cache_disabled())
		ret = true;

	return ret;
}

 
void folio_rotate_reclaimable(struct folio *folio)
{
	if (!folio_test_locked(folio) && !folio_test_dirty(folio) &&
	    !folio_test_unevictable(folio) && folio_test_lru(folio)) {
		struct pagevec *pvec;
		unsigned long flags;

		folio_get(folio);
		local_lock_irqsave(&lru_rotate.lock, flags);
		pvec = this_cpu_ptr(&lru_rotate.pvec);
		if (pagevec_add_and_need_flush(pvec, &folio->page))
			pagevec_lru_move_fn(pvec, pagevec_move_tail_fn);
		local_unlock_irqrestore(&lru_rotate.lock, flags);
	}
}

/* Stub: lru_note_cost not used externally in minimal kernel */
void lru_note_cost(struct lruvec *lruvec, bool file, unsigned int nr_pages)
{
}

void lru_note_cost_folio(struct folio *folio) { }

static void __folio_activate(struct folio *folio, struct lruvec *lruvec)
{
	if (!folio_test_active(folio) && !folio_test_unevictable(folio)) {
		long nr_pages = folio_nr_pages(folio);

		lruvec_del_folio(lruvec, folio);
		folio_set_active(folio);
		lruvec_add_folio(lruvec, folio);

		__count_vm_events(PGACTIVATE, nr_pages);
		__count_memcg_events(lruvec_memcg(lruvec), PGACTIVATE,
				     nr_pages);
	}
}

static inline void activate_page_drain(int cpu)
{
}

static void folio_activate(struct folio *folio)
{
	struct lruvec *lruvec;

	if (folio_test_clear_lru(folio)) {
		lruvec = folio_lruvec_lock_irq(folio);
		__folio_activate(folio, lruvec);
		unlock_page_lruvec_irq(lruvec);
		folio_set_lru(folio);
	}
}

static void __lru_cache_activate_folio(struct folio *folio)
{
	struct pagevec *pvec;
	int i;

	local_lock(&lru_pvecs.lock);
	pvec = this_cpu_ptr(&lru_pvecs.lru_add);

	 
	for (i = pagevec_count(pvec) - 1; i >= 0; i--) {
		struct page *pagevec_page = pvec->pages[i];

		if (pagevec_page == &folio->page) {
			folio_set_active(folio);
			break;
		}
	}

	local_unlock(&lru_pvecs.lock);
}

 
void folio_mark_accessed(struct folio *folio)
{
	if (!folio_test_referenced(folio)) {
		folio_set_referenced(folio);
	} else if (folio_test_unevictable(folio)) {
		 
	} else if (!folio_test_active(folio)) {
		 
		if (folio_test_lru(folio))
			folio_activate(folio);
		else
			__lru_cache_activate_folio(folio);
		folio_clear_referenced(folio);
		workingset_activation(folio);
	}
	if (folio_test_idle(folio))
		folio_clear_idle(folio);
}

 
void folio_add_lru(struct folio *folio)
{
	struct pagevec *pvec;

	VM_BUG_ON_FOLIO(folio_test_active(folio) && folio_test_unevictable(folio), folio);
	VM_BUG_ON_FOLIO(folio_test_lru(folio), folio);

	folio_get(folio);
	local_lock(&lru_pvecs.lock);
	pvec = this_cpu_ptr(&lru_pvecs.lru_add);
	if (pagevec_add_and_need_flush(pvec, &folio->page))
		__pagevec_lru_add(pvec);
	local_unlock(&lru_pvecs.lock);
}

 
void lru_cache_add_inactive_or_unevictable(struct page *page,
					 struct vm_area_struct *vma)
{
	VM_BUG_ON_PAGE(PageLRU(page), page);

	if (unlikely((vma->vm_flags & (VM_LOCKED | VM_SPECIAL)) == VM_LOCKED))
		mlock_new_page(page);
	else
		lru_cache_add(page);
}

 
static void lru_deactivate_file_fn(struct page *page, struct lruvec *lruvec)
{
	bool active = PageActive(page);
	int nr_pages = thp_nr_pages(page);

	if (PageUnevictable(page))
		return;

	 
	if (page_mapped(page))
		return;

	del_page_from_lru_list(page, lruvec);
	ClearPageActive(page);
	ClearPageReferenced(page);

	if (PageWriteback(page) || PageDirty(page)) {
		 
		add_page_to_lru_list(page, lruvec);
		SetPageReclaim(page);
	} else {
		 
		add_page_to_lru_list_tail(page, lruvec);
		__count_vm_events(PGROTATED, nr_pages);
	}

	if (active) {
		__count_vm_events(PGDEACTIVATE, nr_pages);
		__count_memcg_events(lruvec_memcg(lruvec), PGDEACTIVATE,
				     nr_pages);
	}
}

static void lru_deactivate_fn(struct page *page, struct lruvec *lruvec)
{
	if (PageActive(page) && !PageUnevictable(page)) {
		int nr_pages = thp_nr_pages(page);

		del_page_from_lru_list(page, lruvec);
		ClearPageActive(page);
		ClearPageReferenced(page);
		add_page_to_lru_list(page, lruvec);

		__count_vm_events(PGDEACTIVATE, nr_pages);
		__count_memcg_events(lruvec_memcg(lruvec), PGDEACTIVATE,
				     nr_pages);
	}
}

static void lru_lazyfree_fn(struct page *page, struct lruvec *lruvec)
{
	if (PageAnon(page) && PageSwapBacked(page) &&
	    !PageSwapCache(page) && !PageUnevictable(page)) {
		int nr_pages = thp_nr_pages(page);

		del_page_from_lru_list(page, lruvec);
		ClearPageActive(page);
		ClearPageReferenced(page);
		 
		ClearPageSwapBacked(page);
		add_page_to_lru_list(page, lruvec);

		__count_vm_events(PGLAZYFREE, nr_pages);
		__count_memcg_events(lruvec_memcg(lruvec), PGLAZYFREE,
				     nr_pages);
	}
}

 
void lru_add_drain_cpu(int cpu)
{
	struct pagevec *pvec = &per_cpu(lru_pvecs.lru_add, cpu);

	if (pagevec_count(pvec))
		__pagevec_lru_add(pvec);

	pvec = &per_cpu(lru_rotate.pvec, cpu);
	 
	if (data_race(pagevec_count(pvec))) {
		unsigned long flags;

		 
		local_lock_irqsave(&lru_rotate.lock, flags);
		pagevec_lru_move_fn(pvec, pagevec_move_tail_fn);
		local_unlock_irqrestore(&lru_rotate.lock, flags);
	}

	pvec = &per_cpu(lru_pvecs.lru_deactivate_file, cpu);
	if (pagevec_count(pvec))
		pagevec_lru_move_fn(pvec, lru_deactivate_file_fn);

	pvec = &per_cpu(lru_pvecs.lru_deactivate, cpu);
	if (pagevec_count(pvec))
		pagevec_lru_move_fn(pvec, lru_deactivate_fn);

	pvec = &per_cpu(lru_pvecs.lru_lazyfree, cpu);
	if (pagevec_count(pvec))
		pagevec_lru_move_fn(pvec, lru_lazyfree_fn);

	activate_page_drain(cpu);
}

 
void deactivate_file_folio(struct folio *folio)
{
	struct pagevec *pvec;

	 
	if (folio_test_unevictable(folio))
		return;

	folio_get(folio);
	local_lock(&lru_pvecs.lock);
	pvec = this_cpu_ptr(&lru_pvecs.lru_deactivate_file);

	if (pagevec_add_and_need_flush(pvec, &folio->page))
		pagevec_lru_move_fn(pvec, lru_deactivate_file_fn);
	local_unlock(&lru_pvecs.lock);
}

void lru_add_drain(void)
{
	local_lock(&lru_pvecs.lock);
	lru_add_drain_cpu(smp_processor_id());
	local_unlock(&lru_pvecs.lock);
	mlock_page_drain_local();
}

atomic_t lru_disable_count = ATOMIC_INIT(0);

 
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
		if (is_huge_zero_page(page))
			continue;

		if (is_zone_device_page(page)) {
			if (lruvec) {
				unlock_page_lruvec_irqrestore(lruvec, flags);
				lruvec = NULL;
			}
			if (put_devmap_managed_page(page))
				continue;
			if (put_page_testzero(page))
				free_zone_device_page(page);
			continue;
		}

		if (!put_page_testzero(page))
			continue;

		if (PageCompound(page)) {
			if (lruvec) {
				unlock_page_lruvec_irqrestore(lruvec, flags);
				lruvec = NULL;
			}
			__put_compound_page(page);
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
			count_vm_event(UNEVICTABLE_PGCLEARED);
		}

		list_add(&page->lru, &pages_to_free);
	}
	if (lruvec)
		unlock_page_lruvec_irqrestore(lruvec, flags);

	mem_cgroup_uncharge_list(&pages_to_free);
	free_unref_page_list(&pages_to_free);
}

 
void __pagevec_release(struct pagevec *pvec)
{
	if (!pvec->percpu_pvec_drained) {
		lru_add_drain();
		pvec->percpu_pvec_drained = true;
	}
	release_pages(pvec->pages, pagevec_count(pvec));
	pagevec_reinit(pvec);
}

static void __pagevec_lru_add_fn(struct folio *folio, struct lruvec *lruvec)
{
	int was_unevictable = folio_test_clear_unevictable(folio);
	long nr_pages = folio_nr_pages(folio);

	VM_BUG_ON_FOLIO(folio_test_lru(folio), folio);

	folio_set_lru(folio);
	 
	if (folio_evictable(folio)) {
		if (was_unevictable)
			__count_vm_events(UNEVICTABLE_PGRESCUED, nr_pages);
	} else {
		folio_clear_active(folio);
		folio_set_unevictable(folio);
		 
		folio->mlock_count = 0;
		if (!was_unevictable)
			__count_vm_events(UNEVICTABLE_PGCULLED, nr_pages);
	}

	lruvec_add_folio(lruvec, folio);
}

 
void __pagevec_lru_add(struct pagevec *pvec)
{
	int i;
	struct lruvec *lruvec = NULL;
	unsigned long flags = 0;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct folio *folio = page_folio(pvec->pages[i]);

		lruvec = folio_lruvec_relock_irqsave(folio, lruvec, &flags);
		__pagevec_lru_add_fn(folio, lruvec);
	}
	if (lruvec)
		unlock_page_lruvec_irqrestore(lruvec, flags);
	release_pages(pvec->pages, pvec->nr);
	pagevec_reinit(pvec);
}

 
void folio_batch_remove_exceptionals(struct folio_batch *fbatch)
{
	unsigned int i, j;

	for (i = 0, j = 0; i < folio_batch_count(fbatch); i++) {
		struct folio *folio = fbatch->folios[i];
		if (!xa_is_value(folio))
			fbatch->folios[j++] = folio;
	}
	fbatch->nr = j;
}

 
unsigned pagevec_lookup_range(struct pagevec *pvec,
		struct address_space *mapping, pgoff_t *start, pgoff_t end)
{
	pvec->nr = find_get_pages_range(mapping, start, end, PAGEVEC_SIZE,
					pvec->pages);
	return pagevec_count(pvec);
}

unsigned pagevec_lookup_range_tag(struct pagevec *pvec,
		struct address_space *mapping, pgoff_t *index, pgoff_t end,
		xa_mark_t tag)
{
	pvec->nr = find_get_pages_range_tag(mapping, index, end, tag,
					PAGEVEC_SIZE, pvec->pages);
	return pagevec_count(pvec);
}

 
void __init swap_setup(void)
{
	unsigned long megs = totalram_pages() >> (20 - PAGE_SHIFT);

	 
	if (megs < 16)
		page_cluster = 2;
	else
		page_cluster = 3;
	 
}
