/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifndef __GENERATING_BOUNDS_H

#include <linux/nodemask.h>
#include <linux/page-flags-layout.h>

#include <linux/page-flags.h>

#define MAX_ORDER 11
#define MAX_ORDER_NR_PAGES (1 << (MAX_ORDER - 1))

/*
 * PAGE_ALLOC_COSTLY_ORDER is the order at which allocations are deemed
 * costly to service.  That is between allocation orders which should
 * coalesce naturally under reasonable reclaim pressure and those which
 * will not.
 */
#define PAGE_ALLOC_COSTLY_ORDER 3

#define pageblock_nr_pages (1UL << (MAX_ORDER - 1))

struct free_area {
	struct list_head free_list;
};

struct pglist_data;

#define ZONE_PADDING(name)

enum zone_stat_item { NR_FREE_PAGES, NR_VM_ZONE_STAT_ITEMS };

#endif

enum zone_type { ZONE_NORMAL, __MAX_NR_ZONES };

#ifndef __GENERATING_BOUNDS_H

struct zone {
	struct pglist_data *zone_pgdat;

	/* zone_start_pfn == zone_start_paddr >> PAGE_SHIFT */
	unsigned long zone_start_pfn;
	unsigned long spanned_pages;
	unsigned long present_pages;
	const char *name;
	ZONE_PADDING(_pad1_) struct free_area free_area[MAX_ORDER];

	/* Primarily protects free_area */
	spinlock_t lock;

	/* Write-intensive fields used by compaction and vmstats. */
	ZONE_PADDING(_pad2_)
	ZONE_PADDING(_pad3_) atomic_long_t vm_stat[NR_VM_ZONE_STAT_ITEMS];
} ____cacheline_internodealigned_in_smp;

static inline unsigned long zone_end_pfn(const struct zone *zone)
{
	return zone->zone_start_pfn + zone->spanned_pages;
}

/*
 * The array of struct pages for flatmem.
 * It must be declared for SPARSEMEM as well because there are configurations
 * that rely on that.
 */
extern struct page *mem_map;

/*
 * On NUMA machines, each NUMA node would have a pg_data_t to describe
 * it's memory layout. On UMA machines there is a single pglist_data which
 * describes the whole memory.
 *
 * Memory statistics and page replacement data structures are maintained on a
 * per-zone basis.
 */
typedef struct pglist_data {
	/*
	 * node_zones contains just the zones for THIS node. Not all of the
	 * zones may be populated, but it is the full list. It is referenced by
	 * this node's node_zonelists as well as other node's node_zonelists.
	 */
	struct zone node_zones[MAX_NR_ZONES];
	int nr_zones; /* number of populated zones in this node */
	struct page *node_mem_map;
	unsigned long node_start_pfn;
	unsigned long node_spanned_pages; /* total size of physical page
					     range, including holes */
	int node_id;
} pg_data_t;

static inline unsigned long pgdat_end_pfn(pg_data_t *pgdat)
{
	return pgdat->node_start_pfn + pgdat->node_spanned_pages;
}

/*
 * zone_idx() returns 0 for the ZONE_DMA zone, 1 for the ZONE_NORMAL zone, etc.
 */
#define zone_idx(zone) ((zone) - (zone)->zone_pgdat->node_zones)

/* Returns true if a zone has memory */
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

#endif
#endif
