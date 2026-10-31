#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifndef __ASSEMBLY__
#ifndef __GENERATING_BOUNDS_H

#include <linux/nodemask.h>
#include <linux/page-flags-layout.h>

#include <linux/page-flags.h>

/* CONFIG_FORCE_MAX_ZONEORDER is unset in this config, so the #else branch was
 * statically dead; MAX_ORDER is unconditionally 11. */
#define MAX_ORDER 11
#define MAX_ORDER_NR_PAGES (1 << (MAX_ORDER - 1))

#define PAGE_ALLOC_COSTLY_ORDER 3

#define pageblock_nr_pages (1UL << (MAX_ORDER - 1))

struct free_area {
	struct list_head free_list;
	unsigned long nr_free;
};

struct pglist_data;

#define ZONE_PADDING(name)

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

#endif

enum zone_type { ZONE_NORMAL, ZONE_MOVABLE, __MAX_NR_ZONES };

#ifndef __GENERATING_BOUNDS_H

struct zone {
	struct pglist_data *zone_pgdat;
	unsigned long zone_start_pfn;
	atomic_long_t managed_pages;
	unsigned long spanned_pages, present_pages;
	const char *name;
	ZONE_PADDING(_pad1_) struct free_area free_area[MAX_ORDER];
	spinlock_t lock;
	ZONE_PADDING(_pad2_)
	ZONE_PADDING(_pad3_) atomic_long_t vm_stat[NR_VM_ZONE_STAT_ITEMS];
} ____cacheline_internodealigned_in_smp;

static inline unsigned long zone_end_pfn(const struct zone *zone)
{
	return zone->zone_start_pfn + zone->spanned_pages;
}

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
	unsigned long node_start_pfn, node_spanned_pages;
	int node_id;
} pg_data_t;

static inline unsigned long pgdat_end_pfn(pg_data_t *pgdat)
{
	return pgdat->node_start_pfn + pgdat->node_spanned_pages;
}

void build_all_zonelists(pg_data_t *pgdat);
extern void init_currently_empty_zone(struct zone *zone,
				      unsigned long start_pfn,
				      unsigned long size);

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

static inline struct zone *zonelist_zone(struct zoneref *zoneref)
{
	return zoneref->zone;
}

static inline int zonelist_zone_idx(struct zoneref *zoneref)
{
	return zoneref->zone_idx;
}

struct zoneref *__next_zones_zonelist(struct zoneref *z,
				      enum zone_type highest_zoneidx);

static __always_inline struct zoneref *
next_zones_zonelist(struct zoneref *z, enum zone_type highest_zoneidx)
{
	if (likely(zonelist_zone_idx(z) <= highest_zoneidx))
		return z;
	return __next_zones_zonelist(z, highest_zoneidx);
}

static inline struct zoneref *
first_zones_zonelist(struct zonelist *zonelist, enum zone_type highest_zoneidx)
{
	return next_zones_zonelist(zonelist->_zonerefs, highest_zoneidx);
}

#define for_next_zone_zonelist(zone, z, highidx) \
	for (zone = z->zone; zone;               \
	     z = next_zones_zonelist(++z, highidx), zone = zonelist_zone(z))

#define sparse_init() \
	do {          \
	} while (0)

#endif
#endif
#endif
