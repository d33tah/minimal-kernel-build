#ifndef _LINUX_VMSTAT_H
#define _LINUX_VMSTAT_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/mmzone.h>
#include <linux/atomic.h>

#include <linux/mmdebug.h>


static inline void zone_page_state_add(long x, struct zone *zone,
				 enum zone_stat_item item)
{
	atomic_long_add(x, &zone->vm_stat[item]);
}

static inline void node_page_state_add(long x, struct pglist_data *pgdat,
				 enum node_stat_item item)
{
	/* per-node vm_stat[] + vm_node_stat[] are write-only on this build
	 * (no node_page_state reader survives) -> no-op. */
}

static inline unsigned long zone_page_state(struct zone *zone,
					enum zone_stat_item item)
{
	long x = atomic_long_read(&zone->vm_stat[item]);
	return x;
}



static inline void __mod_zone_page_state(struct zone *zone,
			enum zone_stat_item item, long delta)
{
	zone_page_state_add(delta, zone, item);
}

static inline void __mod_node_page_state(struct pglist_data *pgdat,
			enum node_stat_item item, int delta)
{
	if (vmstat_item_in_bytes(item)) {

		VM_WARN_ON_ONCE(delta & (PAGE_SIZE - 1));
		delta >>= PAGE_SHIFT;
	}

	node_page_state_add(delta, pgdat, item);
}

static inline void __dec_zone_state(struct zone *zone, enum zone_stat_item item)
{
	atomic_long_dec(&zone->vm_stat[item]);
}

static inline void __dec_zone_page_state(struct page *page,
			enum zone_stat_item item)
{
	__dec_zone_state(page_zone(page), item);
}

#define dec_zone_page_state __dec_zone_page_state
#define mod_zone_page_state __mod_zone_page_state

#define mod_node_page_state __mod_node_page_state


static inline void __mod_zone_freepage_state(struct zone *zone, int nr_pages,
					     int migratetype)
{
	__mod_zone_page_state(zone, NR_FREE_PAGES, nr_pages);
	if (is_migrate_cma(migratetype))
		__mod_zone_page_state(zone, NR_FREE_CMA_PAGES, nr_pages);
}


static inline void __mod_lruvec_state(struct lruvec *lruvec,
				      enum node_stat_item idx, int val)
{
	__mod_node_page_state(lruvec_pgdat(lruvec), idx, val);
}

static inline void __mod_lruvec_page_state(struct page *page,
					   enum node_stat_item idx, int val)
{
	__mod_node_page_state(page_pgdat(page), idx, val);
}

static inline void mod_lruvec_page_state(struct page *page,
					 enum node_stat_item idx, int val)
{
	mod_node_page_state(page_pgdat(page), idx, val);
}


static inline void inc_lruvec_page_state(struct page *page,
					 enum node_stat_item idx)
{
	mod_lruvec_page_state(page, idx, 1);
}

static inline void dec_lruvec_page_state(struct page *page,
					 enum node_stat_item idx)
{
	mod_lruvec_page_state(page, idx, -1);
}

static inline void __lruvec_stat_mod_folio(struct folio *folio,
					   enum node_stat_item idx, int val)
{
	__mod_lruvec_page_state(&folio->page, idx, val);
}


#endif
