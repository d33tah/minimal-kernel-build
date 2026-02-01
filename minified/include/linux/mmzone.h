#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifndef __ASSEMBLY__
#ifndef __GENERATING_BOUNDS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/numa.h>
#include <linux/init.h>
#include <linux/seqlock.h>
#include <linux/nodemask.h>
#include <linux/page-flags-layout.h>

/* --- 2025-12-08 00:16 --- Inlined from pageblock-flags.h */
#define PB_migratetype_bits 3
enum pageblock_bits {
	PB_migrate,
	PB_migrate_end = PB_migrate + PB_migratetype_bits - 1,
	PB_migrate_skip,
	NR_PAGEBLOCK_BITS
};

#define pageblock_order (MAX_ORDER - 1)
#define pageblock_nr_pages (1UL << pageblock_order)

unsigned long get_pfnblock_flags_mask(const struct page *page,
				      unsigned long pfn, unsigned long mask);
/* end pageblock-flags.h */
#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/local_lock.h>
#include <asm/page.h>

/* CONFIG_FORCE_MAX_ZONEORDER not defined */
#define MAX_ORDER 11
#define MAX_ORDER_NR_PAGES (1 << (MAX_ORDER - 1))

#define PAGE_ALLOC_COSTLY_ORDER 3

enum migratetype {
	MIGRATE_UNMOVABLE,
	MIGRATE_MOVABLE,
	MIGRATE_RECLAIMABLE,
	MIGRATE_PCPTYPES,
	MIGRATE_HIGHATOMIC = MIGRATE_PCPTYPES,
	MIGRATE_TYPES
};

#define is_migrate_cma(migratetype) false

#define for_each_migratetype_order(order, type)     \
	for (order = 0; order < MAX_ORDER; order++) \
		for (type = 0; type < MIGRATE_TYPES; type++)

/* page_group_by_mobility_disabled extern removed - only used in page_alloc.c */

#define MIGRATETYPE_MASK ((1UL << PB_migratetype_bits) - 1)

/* get_pageblock_migratetype removed - unused */

struct free_area {
	struct list_head free_list[MIGRATE_TYPES];
	unsigned long nr_free;
};

static inline struct page *get_page_from_free_area(struct free_area *area,
						   int migratetype)
{
	return list_first_entry_or_null(&area->free_list[migratetype],
					struct page, lru);
}

static inline bool free_area_empty(struct free_area *area, int migratetype)
{
	return list_empty(&area->free_list[migratetype]);
}

struct pglist_data;

#define ZONE_PADDING(name)

/* NR_VM_NUMA_EVENT_ITEMS removed - unused */

enum zone_stat_item {

	NR_FREE_PAGES,
	NR_ZONE_LRU_BASE,
	NR_ZONE_INACTIVE_ANON = NR_ZONE_LRU_BASE,
	NR_ZONE_ACTIVE_ANON,
	NR_ZONE_INACTIVE_FILE,
	NR_ZONE_ACTIVE_FILE,
	NR_ZONE_UNEVICTABLE,
	NR_ZONE_WRITE_PENDING,
	NR_MLOCK,
	NR_BOUNCE,
	NR_FREE_CMA_PAGES,
	NR_VM_ZONE_STAT_ITEMS
};

enum node_stat_item {
	NR_LRU_BASE,
	NR_INACTIVE_ANON = NR_LRU_BASE,
	NR_ACTIVE_ANON,
	NR_INACTIVE_FILE,
	NR_ACTIVE_FILE,
	NR_UNEVICTABLE,
	NR_SLAB_RECLAIMABLE_B,
	NR_SLAB_UNRECLAIMABLE_B,
	NR_ISOLATED_ANON,
	NR_ISOLATED_FILE,
	WORKINGSET_NODES,
	WORKINGSET_REFAULT_BASE,
	WORKINGSET_REFAULT_ANON = WORKINGSET_REFAULT_BASE,
	WORKINGSET_REFAULT_FILE,
	WORKINGSET_ACTIVATE_BASE,
	WORKINGSET_ACTIVATE_ANON = WORKINGSET_ACTIVATE_BASE,
	WORKINGSET_ACTIVATE_FILE,
	WORKINGSET_RESTORE_BASE,
	WORKINGSET_RESTORE_ANON = WORKINGSET_RESTORE_BASE,
	WORKINGSET_RESTORE_FILE,
	WORKINGSET_NODERECLAIM,
	NR_ANON_MAPPED,
	NR_FILE_MAPPED,
	NR_FILE_PAGES,
	NR_FILE_DIRTY,
	NR_WRITEBACK,
	NR_WRITEBACK_TEMP,
	NR_SHMEM,
	NR_SHMEM_THPS,
	NR_SHMEM_PMDMAPPED,
	NR_FILE_THPS,
	NR_FILE_PMDMAPPED,
	NR_ANON_THPS,
	NR_VMSCAN_WRITE,
	NR_VMSCAN_IMMEDIATE,
	NR_DIRTIED,
	NR_WRITTEN,
	NR_THROTTLED_WRITTEN,
	NR_KERNEL_MISC_RECLAIMABLE,
	NR_FOLL_PIN_ACQUIRED,
	NR_FOLL_PIN_RELEASED,
	NR_KERNEL_STACK_KB,
	NR_PAGETABLE,
	NR_VM_NODE_STAT_ITEMS
};

static __always_inline bool vmstat_item_in_bytes(int idx)
{
	return (idx == NR_SLAB_RECLAIMABLE_B || idx == NR_SLAB_UNRECLAIMABLE_B);
}

#define LRU_BASE 0
#define LRU_ACTIVE 1
#define LRU_FILE 2

enum lru_list {
	LRU_INACTIVE_ANON = LRU_BASE,
	LRU_ACTIVE_ANON = LRU_BASE + LRU_ACTIVE,
	LRU_INACTIVE_FILE = LRU_BASE + LRU_FILE,
	LRU_ACTIVE_FILE = LRU_BASE + LRU_FILE + LRU_ACTIVE,
	LRU_UNEVICTABLE,
	NR_LRU_LISTS
};

/* vmscan_throttle_state enum removed - reclaim_wait removed */

#define for_each_lru(lru) for (lru = 0; lru < NR_LRU_LISTS; lru++)

struct lruvec {
	struct list_head lists[NR_LRU_LISTS];

	spinlock_t lru_lock;

	/* anon_cost, file_cost, nonresident_age, flags removed - never used */
};

/* isolate_mode_t removed - unused */

enum zone_watermarks {
	WMARK_MIN,
	WMARK_LOW,
	WMARK_HIGH,
	/* WMARK_PROMO removed - unused */
	NR_WMARK
};

#define NR_PCP_LISTS (MIGRATE_PCPTYPES * (PAGE_ALLOC_COSTLY_ORDER + 1))

/* high_wmark_pages removed - unused */
#define wmark_pages(z, i) (z->_watermark[i] + z->watermark_boost)

struct per_cpu_pages {
	int count;
	/* high removed - write-only, never read */
	int batch;
	struct list_head lists[NR_PCP_LISTS];
};

struct per_cpu_zonestat {};
/* per_cpu_nodestat removed - never referenced */

#endif

enum zone_type {

	ZONE_NORMAL,

	ZONE_MOVABLE,
	__MAX_NR_ZONES

};

#ifndef __GENERATING_BOUNDS_H

struct zone {
	unsigned long _watermark[NR_WMARK];
	unsigned long watermark_boost;

	/* nr_reserved_highatomic removed - never used */

	long lowmem_reserve[MAX_NR_ZONES];

	struct pglist_data *zone_pgdat;
	struct per_cpu_pages __percpu *per_cpu_pageset;
	struct per_cpu_zonestat __percpu *per_cpu_zonestats;

	/* pageset_high, pageset_batch removed - write-only cache fields */

	unsigned long *pageblock_flags;

	unsigned long zone_start_pfn;

	atomic_long_t managed_pages;
	unsigned long spanned_pages;
	unsigned long present_pages;

	const char *name;

	ZONE_PADDING(_pad1_)

	struct free_area free_area[MAX_ORDER];

	/* flags removed - never used */

	spinlock_t lock;

	/* contiguous removed - write-only */

	ZONE_PADDING(_pad2_)

	atomic_long_t vm_stat[NR_VM_ZONE_STAT_ITEMS];
} ____cacheline_internodealigned_in_smp;

/* pgdat_flags and zone_flags enums removed - unused */
/* zone_managed_pages, zone_end_pfn, zone_spans_pfn removed - never called */

#define MAX_ZONES_PER_ZONELIST (MAX_NUMNODES * MAX_NR_ZONES)

enum { ZONELIST_FALLBACK, MAX_ZONELISTS };

struct zoneref {
	struct zone *zone;
	int zone_idx;
};

struct zonelist {
	struct zoneref _zonerefs[MAX_ZONES_PER_ZONELIST + 1];
};

extern struct page *mem_map;

typedef struct pglist_data {
	struct zone node_zones[MAX_NR_ZONES];

	struct zonelist node_zonelists[MAX_ZONELISTS];

	int nr_zones;
	struct page *node_mem_map;
	unsigned long node_start_pfn;
	unsigned long node_present_pages;
	unsigned long node_spanned_pages;
	int node_id;

	/* kswapd_wait, pfmemalloc_wait, reclaim_wait, nr_writeback_throttled,
	   kswapd_order, kswapd_highest_zoneidx, totalreserve_pages, kswapd removed */

	ZONE_PADDING(_pad1_)

	struct lruvec __lruvec;

	/* flags removed - never used */

	ZONE_PADDING(_pad2_)

	/* per_cpu_nodestats removed - only assigned, never read */
	atomic_long_t vm_stat[NR_VM_NODE_STAT_ITEMS];
} pg_data_t;

#define node_present_pages(nid) (NODE_DATA(nid)->node_present_pages)
#define node_spanned_pages(nid) (NODE_DATA(nid)->node_spanned_pages)

#define node_start_pfn(nid) (NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid) pgdat_end_pfn(NODE_DATA(nid))

static inline unsigned long pgdat_end_pfn(pg_data_t *pgdat)
{
	return pgdat->node_start_pfn + pgdat->node_spanned_pages;
}

#include <linux/memory_hotplug.h>

void build_all_zonelists(pg_data_t *pgdat);
enum meminit_context {
	MEMINIT_EARLY,
	MEMINIT_HOTPLUG,
};

extern void init_currently_empty_zone(struct zone *zone,
				      unsigned long start_pfn,
				      unsigned long size);

extern void lruvec_init(struct lruvec *lruvec);

static inline struct pglist_data *lruvec_pgdat(struct lruvec *lruvec)
{
	return container_of(lruvec, struct pglist_data, __lruvec);
}

#define zone_idx(zone) ((zone) - (zone)->zone_pgdat->node_zones)

static inline bool populated_zone(struct zone *zone)
{
	return zone->present_pages;
}

static inline int zone_to_nid(struct zone *zone)
{
	return 0;
}

/* movable_zone extern removed - only used in page_alloc.c */

/* is_highmem_idx removed - never called */

extern struct pglist_data contig_page_data;
static inline struct pglist_data *NODE_DATA(int nid)
{
	return &contig_page_data;
}

extern struct pglist_data *first_online_pgdat(void);
/* next_online_pgdat removed - never called */
extern struct zone *next_zone(struct zone *zone);

/* for_each_online_pgdat, for_each_zone removed - never used */

#define for_each_populated_zone(zone)                         \
	for (zone = (first_online_pgdat())->node_zones; zone; \
	     zone = next_zone(zone))                          \
		if (!populated_zone(zone))                    \
			;                                     \
		else

static inline struct zone *zonelist_zone(struct zoneref *zoneref)
{
	return zoneref->zone;
}

static inline int zonelist_zone_idx(struct zoneref *zoneref)
{
	return zoneref->zone_idx;
}

struct zoneref *__next_zones_zonelist(struct zoneref *z,
				      enum zone_type highest_zoneidx,
				      nodemask_t *nodes);

static __always_inline struct zoneref *
next_zones_zonelist(struct zoneref *z, enum zone_type highest_zoneidx,
		    nodemask_t *nodes)
{
	if (likely(!nodes && zonelist_zone_idx(z) <= highest_zoneidx))
		return z;
	return __next_zones_zonelist(z, highest_zoneidx, nodes);
}

static inline struct zoneref *
first_zones_zonelist(struct zonelist *zonelist, enum zone_type highest_zoneidx,
		     nodemask_t *nodes)
{
	return next_zones_zonelist(zonelist->_zonerefs, highest_zoneidx, nodes);
}

#define for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, nodemask) \
	for (z = first_zones_zonelist(zlist, highidx, nodemask),           \
	    zone = zonelist_zone(z);                                       \
	     zone; z = next_zones_zonelist(++z, highidx, nodemask),        \
	    zone = zonelist_zone(z))

#define for_next_zone_zonelist_nodemask(zone, z, highidx, nodemask) \
	for (zone = z->zone; zone;                                  \
	     z = next_zones_zonelist(++z, highidx, nodemask),       \
	    zone = zonelist_zone(z))

#define for_each_zone_zonelist(zone, z, zlist, highidx) \
	for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, NULL)

/* pfn_to_nid removed - unused */
#define sparse_init() \
	do {          \
	} while (0)

#endif
#endif
#endif
