/* Stub trace functions for minimal kernel */
#ifndef _TRACE_STUBS_H
#define _TRACE_STUBS_H

#include <linux/types.h>

/* Writeback trace stubs */
static inline void trace_writeback_bdi_register(void *bdi) {}
static inline void trace_global_dirty_state(unsigned long bg_thresh, unsigned long dirty_thresh) {}
static inline void trace_bdi_dirty_ratelimit(void *wb, unsigned long dirty_rate, unsigned long task_ratelimit) {}
static inline void trace_balance_dirty_pages(void *wb, unsigned long thresh, unsigned long bg_thresh, unsigned long dirty, unsigned long bdi_thresh, unsigned long bdi_dirty, unsigned long dirty_ratelimit, unsigned long task_ratelimit, unsigned long dirtied, unsigned long period, long pause, unsigned long start_time) {}
static inline void trace_wbc_writepage(void *wbc, void *bdi) {}
static inline void trace_writeback_dirty_folio(void *folio, void *mapping) {}
static inline void trace_folio_wait_writeback(void *folio, void *mapping) {}
static inline void trace_writeback_lazytime_iput(void *inode) {}
static inline void trace_mm_lru_activate(void *folio) {}
static inline void trace_mm_lru_insertion(void *folio) {}

/* VM scan trace stubs */
static inline void trace_mm_shrink_slab_start(void *shrinker, void *shrinkctl, long nr) {}
static inline void trace_mm_shrink_slab_end(void *shrinker, void *shrinkctl, long nid, long freed, long new_nr, long total_scan) {}
static inline void trace_mm_vmscan_throttled(int node_id, unsigned long usecs) {}
static inline void trace_mm_vmscan_write_folio(void *folio) {}
static inline void trace_mm_vmscan_lru_isolate(int reclaim_idx, int order, long nr_to_scan) {}
static inline void trace_mm_vmscan_lru_shrink_inactive(int node_id, long nr_taken, long nr_activate) {}
static inline void trace_mm_vmscan_lru_shrink_active(int node_id, unsigned long nr_taken, unsigned long nr_activate, unsigned long nr_deactivate, unsigned long nr_rotated, int priority, int file) {}
static inline void trace_mm_vmscan_direct_reclaim_begin(int order, unsigned int gfp_mask) {}
static inline void trace_mm_vmscan_direct_reclaim_end(long nr_reclaimed) {}
static inline void trace_mm_vmscan_kswapd_sleep(int node_id) {}
static inline void trace_mm_vmscan_kswapd_wake(int node_id, int highest_zoneidx, int order) {}
static inline void trace_mm_vmscan_wakeup_kswapd(int node_id, int highest_zoneidx, int order, unsigned int gfp_flags) {}

/* Percpu trace stubs */
static inline void trace_percpu_create_chunk(void *base_addr) {}
static inline void trace_percpu_destroy_chunk(void *base_addr) {}
static inline void trace_percpu_alloc_percpu(unsigned long _ret_ip_, int reserved, int is_atomic, unsigned long size, unsigned long align, void *base_addr, int off, void *ptr, unsigned long full_size, unsigned int gfp) {}
static inline void trace_percpu_alloc_percpu_fail(int reserved, int is_atomic, unsigned long size, unsigned long align) {}
static inline void trace_percpu_free_percpu(void *base_addr, int off, void *ptr) {}

/* Memory management trace stubs */
static inline void trace_rss_stat(void *mm, int member, long count) {}
static inline void trace_vm_unmapped_area(unsigned long addr, void *info) {}
static inline void trace_set_migration_pte(unsigned long address, unsigned long pte, unsigned int order) {}
static inline void trace_mm_page_free(struct page *page, unsigned int order) {}
static inline void trace_mm_page_pcpu_drain(struct page *page, unsigned int order, int mt) {}
static inline void trace_mm_page_alloc_zone_locked(struct page *page, unsigned int order, int migratetype, int pcp) {}
static inline void trace_mm_page_alloc_extfrag(struct page *page, unsigned int order, int current_order, int migratetype, int alloc_migratetype) {}
static inline void trace_mm_page_free_batched(struct page *page) {}
static inline void trace_reclaim_retry_zone(struct zone *zone, unsigned int order, unsigned long reclaimable, unsigned long available, unsigned long min_wmark, int no_progress_loops, int wmark) {}
static inline void trace_mm_page_alloc(struct page *page, unsigned int order, int migratetype, unsigned int alloc_flags) {}
static inline void trace_kmem_cache_alloc(unsigned long call_site, void *objp, size_t size, size_t sli, gfp_t gfp_flags) {}
static inline void trace_kmem_cache_free(unsigned long call_site, void *objp, const char *name) {}
static inline void trace_kmalloc(unsigned long call_site, void *ptr, size_t bytes_req, size_t bytes_alloc, gfp_t gfp_flags) {}
static inline void trace_kfree(unsigned long call_site, const void *ptr) {}

#endif /* _TRACE_STUBS_H */