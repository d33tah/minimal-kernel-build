#ifndef _LINUX_VMSTAT_H
#define _LINUX_VMSTAT_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/mmzone.h>
#include <linux/vm_event_item.h>
#include <linux/atomic.h>
#include <linux/static_key.h>
#include <linux/mmdebug.h>

/* sysctl_stat_interval removed - unused */


struct reclaim_stat;

enum writeback_stat_item { NR_VM_WRITEBACK_STAT_ITEMS };


static inline void count_vm_event(enum vm_event_item item)
{
}
static inline void count_vm_events(enum vm_event_item item, long delta)
{
}
static inline void __count_vm_event(enum vm_event_item item)
{
}
static inline void __count_vm_events(enum vm_event_item item, long delta)
{
}
static inline void vm_events_fold_cpu(int cpu)
{
}


#define count_vm_numa_event(x) do {} while (0)
#define count_vm_numa_events(x, y) do { (void)(y); } while (0)

#define count_vm_tlb_event(x)     do {} while (0)
#define count_vm_tlb_events(x, y) do { (void)(y); } while (0)

#define count_vm_vmacache_event(x) do {} while (0)

#define __count_zid_vm_events(item, zid, delta) \
	__count_vm_events(item##_NORMAL - ZONE_NORMAL + zid, delta)

extern atomic_long_t vm_zone_stat[NR_VM_ZONE_STAT_ITEMS];
extern atomic_long_t vm_node_stat[NR_VM_NODE_STAT_ITEMS];
extern atomic_long_t vm_numa_event[NR_VM_NUMA_EVENT_ITEMS];


static inline void zone_page_state_add(long x, struct zone *zone,
				 enum zone_stat_item item)
{
	atomic_long_add(x, &zone->vm_stat[item]);
	atomic_long_add(x, &vm_zone_stat[item]);
}

static inline void node_page_state_add(long x, struct pglist_data *pgdat,
				 enum node_stat_item item)
{
	atomic_long_add(x, &pgdat->vm_stat[item]);
	atomic_long_add(x, &vm_node_stat[item]);
}

static inline unsigned long global_zone_page_state(enum zone_stat_item item)
{
	long x = atomic_long_read(&vm_zone_stat[item]);
	return x;
}

static inline
unsigned long global_node_page_state_pages(enum node_stat_item item)
{
	long x = atomic_long_read(&vm_node_stat[item]);
	return x;
}

static inline unsigned long global_node_page_state(enum node_stat_item item)
{
	VM_WARN_ON_ONCE(vmstat_item_in_bytes(item));

	return global_node_page_state_pages(item);
}

static inline unsigned long zone_page_state(struct zone *zone,
					enum zone_stat_item item)
{
	long x = atomic_long_read(&zone->vm_stat[item]);
	return x;
}

/* zone_page_state_snapshot removed - unused */

#define sum_zone_node_page_state(node, item) global_zone_page_state(item)
#define node_page_state(node, item) global_node_page_state(item)
#define node_page_state_pages(node, item) global_node_page_state_pages(item)



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

static inline void __inc_zone_state(struct zone *zone, enum zone_stat_item item)
{
	atomic_long_inc(&zone->vm_stat[item]);
	atomic_long_inc(&vm_zone_stat[item]);
}

static inline void __inc_node_state(struct pglist_data *pgdat, enum node_stat_item item)
{
	atomic_long_inc(&pgdat->vm_stat[item]);
	atomic_long_inc(&vm_node_stat[item]);
}

static inline void __dec_zone_state(struct zone *zone, enum zone_stat_item item)
{
	atomic_long_dec(&zone->vm_stat[item]);
	atomic_long_dec(&vm_zone_stat[item]);
}

static inline void __dec_node_state(struct pglist_data *pgdat, enum node_stat_item item)
{
	atomic_long_dec(&pgdat->vm_stat[item]);
	atomic_long_dec(&vm_node_stat[item]);
}

static inline void __inc_zone_page_state(struct page *page,
			enum zone_stat_item item)
{
	__inc_zone_state(page_zone(page), item);
}

static inline void __inc_node_page_state(struct page *page,
			enum node_stat_item item)
{
	__inc_node_state(page_pgdat(page), item);
}


static inline void __dec_zone_page_state(struct page *page,
			enum zone_stat_item item)
{
	__dec_zone_state(page_zone(page), item);
}

static inline void __dec_node_page_state(struct page *page,
			enum node_stat_item item)
{
	__dec_node_state(page_pgdat(page), item);
}


#define inc_zone_page_state __inc_zone_page_state
#define dec_zone_page_state __dec_zone_page_state
#define mod_zone_page_state __mod_zone_page_state

#define inc_node_page_state __inc_node_page_state
#define dec_node_page_state __dec_node_page_state
#define mod_node_page_state __mod_node_page_state

#define inc_zone_state __inc_zone_state
#define inc_node_state __inc_node_state
#define dec_zone_state __dec_zone_state

#define set_pgdat_percpu_threshold(pgdat, callback) { }

static inline void refresh_zone_stat_thresholds(void) { }
static inline void cpu_vm_stats_fold(int cpu) { }
static inline void quiet_vmstat(void) { }

static inline void drain_zonestat(struct zone *zone,
			struct per_cpu_zonestat *pzstats) { }

/* __zone_stat_mod_folio, __zone_stat_add_folio, __zone_stat_sub_folio removed - unused */
/* __node_stat_mod_folio, __node_stat_add_folio, __node_stat_sub_folio removed - unused */

static inline void node_stat_mod_folio(struct folio *folio,
		enum node_stat_item item, long nr)
{
	mod_node_page_state(folio_pgdat(folio), item, nr);
}


static inline void __mod_zone_freepage_state(struct zone *zone, int nr_pages,
					     int migratetype)
{
	__mod_zone_page_state(zone, NR_FREE_PAGES, nr_pages);
	if (is_migrate_cma(migratetype))
		__mod_zone_page_state(zone, NR_FREE_CMA_PAGES, nr_pages);
}

extern const char * const vmstat_text[];



/* mod_lruvec_state removed - unused */

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

/* __inc_lruvec_page_state removed - unused */

static inline void __dec_lruvec_page_state(struct page *page,
					   enum node_stat_item idx)
{
	__mod_lruvec_page_state(page, idx, -1);
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

/* __lruvec_stat_add_folio, __lruvec_stat_sub_folio removed - unused */

#endif  
