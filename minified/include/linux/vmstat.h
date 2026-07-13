#ifndef _LINUX_VMSTAT_H
#define _LINUX_VMSTAT_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/mmzone.h>



static inline void zone_page_state_add(long x, struct zone *zone, enum zone_stat_item item) {
	atomic_long_add(x, &zone->vm_stat[item]); }

static inline unsigned long zone_page_state(struct zone *zone, enum zone_stat_item item) {
	long x = atomic_long_read(&zone->vm_stat[item]);
	return x; }



static inline void __mod_zone_page_state(struct zone *zone, enum zone_stat_item item, long delta) {
	zone_page_state_add(delta, zone, item); }

static inline void __mod_node_page_state(struct pglist_data *pgdat, enum node_stat_item item, int delta) {
	/* per-node vm_stat[] + vm_node_stat[] are write-only on this build
	 * (no node_page_state reader survives) -> no-op. */
}

#define mod_node_page_state __mod_node_page_state


static inline void __mod_zone_freepage_state(struct zone *zone, int nr_pages, int migratetype) {
	__mod_zone_page_state(zone, NR_FREE_PAGES, nr_pages);
	if (is_migrate_cma(migratetype))
		__mod_zone_page_state(zone, NR_FREE_CMA_PAGES, nr_pages); }


static inline void __mod_lruvec_state(struct lruvec *lruvec, enum node_stat_item idx, int val) {
	__mod_node_page_state(lruvec_pgdat(lruvec), idx, val); }

static inline void __mod_lruvec_page_state(struct page *page, enum node_stat_item idx, int val) {
	__mod_node_page_state(page_pgdat(page), idx, val); }

static inline void mod_lruvec_page_state(struct page *page, enum node_stat_item idx, int val) {
	mod_node_page_state(page_pgdat(page), idx, val); }


static inline void inc_lruvec_page_state(struct page *page, enum node_stat_item idx) {
	mod_lruvec_page_state(page, idx, 1); }

static inline void dec_lruvec_page_state(struct page *page, enum node_stat_item idx) {
	mod_lruvec_page_state(page, idx, -1); }

static inline void __lruvec_stat_mod_folio(struct folio *folio, enum node_stat_item idx, int val) {
	__mod_lruvec_page_state(&folio->page, idx, val); }


#endif
