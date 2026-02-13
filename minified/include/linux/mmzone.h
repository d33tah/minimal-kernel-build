#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifndef __ASSEMBLY__
#ifndef __GENERATING_BOUNDS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/numa.h>
#include <linux/init.h>
#include <linux/seqlock.h>
#include <linux/nodemask.h>
/* page-flags-layout.h inlined */
#include <generated/bounds.h>

#ifndef PAGE_FLAGS_LAYOUT_H
#define PAGE_FLAGS_LAYOUT_H

#if MAX_NR_ZONES < 2
#define ZONES_SHIFT 0
#elif MAX_NR_ZONES <= 2
#define ZONES_SHIFT 1
#elif MAX_NR_ZONES <= 4
#define ZONES_SHIFT 2
#elif MAX_NR_ZONES <= 8
#define ZONES_SHIFT 3
#else
#error ZONES_SHIFT "Too many zones configured"
#endif

#define ZONES_WIDTH		ZONES_SHIFT

#ifndef BUILD_VDSO32_64
#define SECTIONS_WIDTH		0

#if ZONES_WIDTH + SECTIONS_WIDTH + NODES_SHIFT <= BITS_PER_LONG - NR_PAGEFLAGS
#define NODES_WIDTH		NODES_SHIFT
#else
#define NODES_WIDTH		0
#endif

#define KASAN_TAG_WIDTH 0

#define LAST_CPUPID_SHIFT 0

#if ZONES_WIDTH + SECTIONS_WIDTH + NODES_WIDTH + KASAN_TAG_WIDTH + LAST_CPUPID_SHIFT \
	<= BITS_PER_LONG - NR_PAGEFLAGS
#define LAST_CPUPID_WIDTH LAST_CPUPID_SHIFT
#else
#define LAST_CPUPID_WIDTH 0
#endif

#if LAST_CPUPID_SHIFT != 0 && LAST_CPUPID_WIDTH == 0
#define LAST_CPUPID_NOT_IN_PAGE_FLAGS
#endif

#if ZONES_WIDTH + SECTIONS_WIDTH + NODES_WIDTH + KASAN_TAG_WIDTH + LAST_CPUPID_WIDTH \
	> BITS_PER_LONG - NR_PAGEFLAGS
#error "Not enough bits in page flags"
#endif

#endif
#endif /* PAGE_FLAGS_LAYOUT_H */

#define PB_migratetype_bits 3
enum pageblock_bits {
	PB_migrate,
	PB_migrate_end = PB_migrate + PB_migratetype_bits - 1,
	PB_migrate_skip,
	NR_PAGEBLOCK_BITS
};

#define pageblock_order (MAX_ORDER - 1)
#define pageblock_nr_pages (1UL << pageblock_order)

#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/local_lock.h>
#include <asm/page.h>

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

#define for_each_migratetype_order(order, type)     \
	for (order = 0; order < MAX_ORDER; order++) \
		for (type = 0; type < MIGRATE_TYPES; type++)

#define MIGRATETYPE_MASK ((1UL << PB_migratetype_bits) - 1)

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

struct pglist_data;

enum zone_stat_item {
	NR_FREE_PAGES,
	NR_ZONE_LRU_BASE,
	NR_MLOCK,
	NR_VM_ZONE_STAT_ITEMS
};

enum node_stat_item {
	NR_LRU_BASE,
	NR_SLAB_RECLAIMABLE_B,
	NR_SLAB_UNRECLAIMABLE_B,
	NR_ANON_MAPPED,
	NR_FILE_MAPPED,
	NR_FILE_PAGES,
	NR_SHMEM,
	NR_KERNEL_STACK_KB,
	NR_PAGETABLE,
	NR_VM_NODE_STAT_ITEMS
};

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

#define for_each_lru(lru) for (lru = 0; lru < NR_LRU_LISTS; lru++)

struct lruvec {
	struct list_head lists[NR_LRU_LISTS];

	spinlock_t lru_lock;

};

enum zone_watermarks {
	WMARK_MIN,
	WMARK_LOW,
	WMARK_HIGH,
	NR_WMARK
};

#define NR_PCP_LISTS (MIGRATE_PCPTYPES * (PAGE_ALLOC_COSTLY_ORDER + 1))

#define wmark_pages(z, i) (z->_watermark[i] + z->watermark_boost)

struct per_cpu_pages {
	int count;
	int batch;
	struct list_head lists[NR_PCP_LISTS];
};

struct per_cpu_zonestat {};

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

	long lowmem_reserve[MAX_NR_ZONES];

	struct pglist_data *zone_pgdat;
	struct per_cpu_pages __percpu *per_cpu_pageset;
	struct per_cpu_zonestat __percpu *per_cpu_zonestats;

	unsigned long *pageblock_flags;

	unsigned long zone_start_pfn;

	atomic_long_t managed_pages;
	unsigned long spanned_pages;
	unsigned long present_pages;

	const char *name;

	struct free_area free_area[MAX_ORDER];

	spinlock_t lock;

	atomic_long_t vm_stat[NR_VM_ZONE_STAT_ITEMS];
} ____cacheline_internodealigned_in_smp;

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

	struct lruvec __lruvec;

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

void build_all_zonelists(pg_data_t *pgdat);
enum meminit_context {
	MEMINIT_EARLY,
	MEMINIT_HOTPLUG,
};

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

extern struct pglist_data contig_page_data;
static inline struct pglist_data *NODE_DATA(int nid)
{
	return &contig_page_data;
}

extern struct pglist_data *first_online_pgdat(void);
extern struct zone *next_zone(struct zone *zone);

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

#define for_next_zone_zonelist_nodemask(zone, z, highidx, nodemask) \
	for (zone = z->zone; zone;                                  \
	     z = next_zones_zonelist(++z, highidx, nodemask),       \
	    zone = zonelist_zone(z))

#define sparse_init() \
	do {          \
	} while (0)

#endif
#endif
#endif
