

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/init.h>
#include <linux/tracepoint.h>
#include <linux/mm_inline.h>
#include <linux/percpu_counter.h>
#include <linux/memremap.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/backing-dev.h>
/* memcontrol.h removed - unused */
#include <linux/gfp.h>
#include <linux/uio.h>
/* hugetlb.h removed - unused */
#include <linux/local_lock.h>

#include "internal.h"

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
	/* lru_deactivate_file, lru_deactivate, lru_lazyfree removed - pvecs never populated */
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
		/* thp_nr_pages inlined - single caller */
		VM_BUG_ON_PGFLAGS(PageTail(page), page);
		int nr_pages = compound_nr(page);

		__ClearPageMlocked(page);
		mod_zone_page_state(page_zone(page), NR_MLOCK, -nr_pages);
	}
}

void __put_page(struct page *page)
{
	/* is_zone_device_page always returns false */
	__page_cache_release(page);
	if (unlikely(PageCompound(page))) {
		/* PageHuge is always false */
		destroy_compound_page(page);
	} else {
		/* mem_cgroup_uncharge is empty stub */
		free_unref_page(page, 0);
	}
}

static void pagevec_lru_move_fn(struct pagevec *pvec,
				void (*move_fn)(struct page *page,
						struct lruvec *lruvec))
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
	}
}

static bool pagevec_add_and_need_flush(struct pagevec *pvec, struct page *page)
{
	/* lru_cache_disabled() call removed - always returns false */
	return !pagevec_add(pvec, page) || PageCompound(page);
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

/* folio_activate, __lru_cache_activate_folio, activate_page_drain inlined/removed */

void folio_mark_accessed(struct folio *folio)
{
	if (!folio_test_referenced(folio)) {
		folio_set_referenced(folio);
	} else if (folio_test_unevictable(folio)) {
	} else if (!folio_test_active(folio)) {
		if (folio_test_lru(folio)) {
			/* folio_activate inlined */
			struct lruvec *lruvec;
			if (folio_test_clear_lru(folio)) {
				lruvec = folio_lruvec_lock_irq(folio);
				if (!folio_test_active(folio) &&
				    !folio_test_unevictable(folio)) {
					lruvec_del_folio(lruvec, folio);
					folio_set_active(folio);
					lruvec_add_folio(lruvec, folio);
				}
				unlock_page_lruvec_irq(lruvec);
				folio_set_lru(folio);
			}
		} else {
			/* __lru_cache_activate_folio inlined */
			struct pagevec *pvec;
			int i;
			local_lock(&lru_pvecs.lock);
			pvec = this_cpu_ptr(&lru_pvecs.lru_add);
			for (i = pagevec_count(pvec) - 1; i >= 0; i--) {
				if (pvec->pages[i] == &folio->page) {
					folio_set_active(folio);
					break;
				}
			}
			local_unlock(&lru_pvecs.lock);
		}
		folio_clear_referenced(folio);
	}
}

void folio_add_lru(struct folio *folio)
{
	struct pagevec *pvec;
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
	/* mlock_new_page removed - empty stub */
	/* lru_cache_add inlined */
	folio_add_lru(page_folio(page));
}

/* lru_deactivate_file_fn, lru_deactivate_fn, lru_lazyfree_fn removed - pvecs never populated */

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

	/* lru_deactivate_file, lru_deactivate, lru_lazyfree, activate_page_drain removed - never populated */
}

/* deactivate_file_folio removed - never called */

void lru_add_drain(void)
{
	local_lock(&lru_pvecs.lock);
	lru_add_drain_cpu(smp_processor_id());
	local_unlock(&lru_pvecs.lock);
}

/* lru_disable_count removed - never modified, always 0 */

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
		/* is_zone_device_page always returns false */
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
	/* mem_cgroup_uncharge_list is empty stub */
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

void __pagevec_lru_add(struct pagevec *pvec)
{
	int i;
	struct lruvec *lruvec = NULL;
	unsigned long flags = 0;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct folio *folio = page_folio(pvec->pages[i]);

		lruvec = folio_lruvec_relock_irqsave(folio, lruvec, &flags);
		/* __pagevec_lru_add_fn inlined */
		(void)folio_test_clear_unevictable(folio);
		folio_set_lru(folio);
		/* folio_evictable inlined - single caller */
		{
			bool evictable;
			struct address_space *mapping;
			rcu_read_lock();
			/* mapping_unevictable inlined - single caller */
			mapping = folio_mapping(folio);
			evictable = !(mapping && test_bit(AS_UNEVICTABLE,
							  &mapping->flags)) &&
				    !folio_test_mlocked(folio);
			rcu_read_unlock();
			if (!evictable) {
				folio_clear_active(folio);
				folio_set_unevictable(folio);
			}
		}
		lruvec_add_folio(lruvec, folio);
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

unsigned pagevec_lookup_range_tag(struct pagevec *pvec,
				  struct address_space *mapping, pgoff_t *index,
				  pgoff_t end, xa_mark_t tag)
{
	pvec->nr = find_get_pages_range_tag(mapping, index, end, tag,
					    PAGEVEC_SIZE, pvec->pages);
	return pagevec_count(pvec);
}
