 
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
#include <linux/pageblock-flags.h>
#include <linux/page-flags-layout.h>
#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/local_lock.h>
#include <asm/page.h>

 
#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 11
#else
#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif
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

 
extern const char * const migratetype_names[MIGRATE_TYPES];

#  define is_migrate_cma(migratetype) false
#  define is_migrate_cma_page(_page) false


static inline bool migratetype_is_mergeable(int mt)
{
	return mt < MIGRATE_PCPTYPES;
}

#define for_each_migratetype_order(order, type) \
	for (order = 0; order < MAX_ORDER; order++) \
		for (type = 0; type < MIGRATE_TYPES; type++)

extern int page_group_by_mobility_disabled;

#define MIGRATETYPE_MASK ((1UL << PB_migratetype_bits) - 1)

#define get_pageblock_migratetype(page)					\
	get_pfnblock_flags_mask(page, page_to_pfn(page), MIGRATETYPE_MASK)

struct free_area {
	struct list_head	free_list[MIGRATE_TYPES];
	unsigned long		nr_free;
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

#define NR_VM_NUMA_EVENT_ITEMS 0

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
#if IS_ENABLED(CONFIG_ZSMALLOC)
	NR_ZSPAGES,		 
#endif
	NR_FREE_CMA_PAGES,
	NR_VM_ZONE_STAT_ITEMS };

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
#if IS_ENABLED(CONFIG_SHADOW_CALL_STACK)
	NR_KERNEL_SCS_KB,	 
#endif
	NR_PAGETABLE,		 
	NR_VM_NODE_STAT_ITEMS
};


static __always_inline bool vmstat_item_in_bytes(int idx)
{
	 
	return (idx == NR_SLAB_RECLAIMABLE_B ||
		idx == NR_SLAB_UNRECLAIMABLE_B);
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

enum vmscan_throttle_state {
	VMSCAN_THROTTLE_WRITEBACK,
	VMSCAN_THROTTLE_ISOLATED,
	VMSCAN_THROTTLE_NOPROGRESS,
	VMSCAN_THROTTLE_CONGESTED,
	NR_VMSCAN_THROTTLE,
};

#define for_each_lru(lru) for (lru = 0; lru < NR_LRU_LISTS; lru++)

static inline bool is_file_lru(enum lru_list lru)
{
	return (lru == LRU_INACTIVE_FILE || lru == LRU_ACTIVE_FILE);
}

static inline bool is_active_lru(enum lru_list lru)
{
	return (lru == LRU_ACTIVE_ANON || lru == LRU_ACTIVE_FILE);
}

#define ANON_AND_FILE 2

enum lruvec_flags {
	LRUVEC_CONGESTED,		 
};

struct lruvec {
	struct list_head		lists[NR_LRU_LISTS];
	 
	spinlock_t			lru_lock;
	 
	unsigned long			anon_cost;
	unsigned long			file_cost;
	 
	atomic_long_t			nonresident_age;
	 
	unsigned long			refaults[ANON_AND_FILE];
	 
	unsigned long			flags;
};

 
#define ISOLATE_UNMAPPED	((__force isolate_mode_t)0x2)
 
#define ISOLATE_ASYNC_MIGRATE	((__force isolate_mode_t)0x4)
 
#define ISOLATE_UNEVICTABLE	((__force isolate_mode_t)0x8)

 
typedef unsigned __bitwise isolate_mode_t;

enum zone_watermarks {
	WMARK_MIN,
	WMARK_LOW,
	WMARK_HIGH,
	WMARK_PROMO,
	NR_WMARK
};

 
#define NR_PCP_THP 0
#define NR_PCP_LISTS (MIGRATE_PCPTYPES * (PAGE_ALLOC_COSTLY_ORDER + 1 + NR_PCP_THP))

 
#define NR_PCP_ORDER_WIDTH 8
#define NR_PCP_ORDER_MASK ((1<<NR_PCP_ORDER_WIDTH) - 1)

#define min_wmark_pages(z) (z->_watermark[WMARK_MIN] + z->watermark_boost)
#define low_wmark_pages(z) (z->_watermark[WMARK_LOW] + z->watermark_boost)
#define high_wmark_pages(z) (z->_watermark[WMARK_HIGH] + z->watermark_boost)
#define wmark_pages(z, i) (z->_watermark[i] + z->watermark_boost)

 
struct per_cpu_pages {
	int count;		 
	int high;		 
	int batch;		 
	short free_factor;	 

	 
	struct list_head lists[NR_PCP_LISTS];
};

struct per_cpu_zonestat {
};

struct per_cpu_nodestat {
	s8 stat_threshold;
	s8 vm_node_stat_diff[NR_VM_NODE_STAT_ITEMS];
};

#endif  

enum zone_type {
	 
	 
	ZONE_NORMAL,
	 
	ZONE_MOVABLE,
	__MAX_NR_ZONES

};

#ifndef __GENERATING_BOUNDS_H

#define ASYNC_AND_SYNC 2

struct zone {
	 

	 
	unsigned long _watermark[NR_WMARK];
	unsigned long watermark_boost;

	unsigned long nr_reserved_highatomic;

	 
	long lowmem_reserve[MAX_NR_ZONES];

	struct pglist_data	*zone_pgdat;
	struct per_cpu_pages	__percpu *per_cpu_pageset;
	struct per_cpu_zonestat	__percpu *per_cpu_zonestats;
	 
	int pageset_high;
	int pageset_batch;

	 
	unsigned long		*pageblock_flags;

	 
	unsigned long		zone_start_pfn;

	 
	atomic_long_t		managed_pages;
	unsigned long		spanned_pages;
	unsigned long		present_pages;

	const char		*name;



	int initialized;

	 
	ZONE_PADDING(_pad1_)

	 
	struct free_area	free_area[MAX_ORDER];

	 
	unsigned long		flags;

	 
	spinlock_t		lock;

	 
	ZONE_PADDING(_pad2_)

	 
	unsigned long percpu_drift_mark;




	bool			contiguous;

	ZONE_PADDING(_pad3_)
	 
	atomic_long_t		vm_stat[NR_VM_ZONE_STAT_ITEMS];
	atomic_long_t		vm_numa_event[NR_VM_NUMA_EVENT_ITEMS];
} ____cacheline_internodealigned_in_smp;

enum pgdat_flags {
	PGDAT_DIRTY,			 
	PGDAT_WRITEBACK,		 
	PGDAT_RECLAIM_LOCKED,		 
};

enum zone_flags {
	ZONE_BOOSTED_WATERMARK,		 
	ZONE_RECLAIM_ACTIVE,		 
};

static inline unsigned long zone_managed_pages(struct zone *zone)
{
	return (unsigned long)atomic_long_read(&zone->managed_pages);
}

static inline unsigned long zone_end_pfn(const struct zone *zone)
{
	return zone->zone_start_pfn + zone->spanned_pages;
}

static inline bool zone_spans_pfn(const struct zone *zone, unsigned long pfn)
{
	return zone->zone_start_pfn <= pfn && pfn < zone_end_pfn(zone);
}

static inline bool zone_is_initialized(struct zone *zone)
{
	return zone->initialized;
}

#define DEF_PRIORITY 12

 
#define MAX_ZONES_PER_ZONELIST (MAX_NUMNODES * MAX_NR_ZONES)

enum {
	ZONELIST_FALLBACK,	 
	MAX_ZONELISTS
};

 
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
	wait_queue_head_t kswapd_wait;
	wait_queue_head_t pfmemalloc_wait;

	 
	wait_queue_head_t reclaim_wait[NR_VMSCAN_THROTTLE];

	atomic_t nr_writeback_throttled; 
	unsigned long nr_reclaim_start;	 
	struct task_struct *kswapd;	 
	int kswapd_order;
	enum zone_type kswapd_highest_zoneidx;

	int kswapd_failures;		 

	 
	unsigned long		totalreserve_pages;


	 
	ZONE_PADDING(_pad1_)



	 

	 
	struct lruvec		__lruvec;

	unsigned long		flags;

	ZONE_PADDING(_pad2_)

	 
	struct per_cpu_nodestat __percpu *per_cpu_nodestats;
	atomic_long_t		vm_stat[NR_VM_NODE_STAT_ITEMS];
} pg_data_t;

#define node_present_pages(nid)	(NODE_DATA(nid)->node_present_pages)
#define node_spanned_pages(nid)	(NODE_DATA(nid)->node_spanned_pages)

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid) pgdat_end_pfn(NODE_DATA(nid))

static inline unsigned long pgdat_end_pfn(pg_data_t *pgdat)
{
	return pgdat->node_start_pfn + pgdat->node_spanned_pages;
}

#include <linux/memory_hotplug.h>

void build_all_zonelists(pg_data_t *pgdat);
void wakeup_kswapd(struct zone *zone, gfp_t gfp_mask, int order,
		   enum zone_type highest_zoneidx);
bool __zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
			 int highest_zoneidx, unsigned int alloc_flags,
			 long free_pages);
bool zone_watermark_ok(struct zone *z, unsigned int order,
		unsigned long mark, int highest_zoneidx,
		unsigned int alloc_flags);
bool zone_watermark_ok_safe(struct zone *z, unsigned int order,
		unsigned long mark, int highest_zoneidx);
 
enum meminit_context {
	MEMINIT_EARLY,
	MEMINIT_HOTPLUG,
};

extern void init_currently_empty_zone(struct zone *zone, unsigned long start_pfn,
				     unsigned long size);

extern void lruvec_init(struct lruvec *lruvec);

static inline struct pglist_data *lruvec_pgdat(struct lruvec *lruvec)
{
	return container_of(lruvec, struct pglist_data, __lruvec);
}


#define zone_idx(zone)		((zone) - (zone)->zone_pgdat->node_zones)


static inline bool managed_zone(struct zone *zone)
{
	return zone_managed_pages(zone);
}

 
static inline bool populated_zone(struct zone *zone)
{
	return zone->present_pages;
}

static inline int zone_to_nid(struct zone *zone)
{
	return 0;
}

static inline void zone_set_nid(struct zone *zone, int nid) {}

extern int movable_zone;

static inline int is_highmem_idx(enum zone_type idx)
{
	return 0;
}

static inline int is_highmem(struct zone *zone)
{
	return 0;
}

 
struct ctl_table;

int min_free_kbytes_sysctl_handler(struct ctl_table *, int, void *, size_t *,
		loff_t *);
int watermark_scale_factor_sysctl_handler(struct ctl_table *, int, void *,
		size_t *, loff_t *);
extern int sysctl_lowmem_reserve_ratio[MAX_NR_ZONES];
int lowmem_reserve_ratio_sysctl_handler(struct ctl_table *, int, void *,
		size_t *, loff_t *);
int percpu_pagelist_high_fraction_sysctl_handler(struct ctl_table *, int,
		void *, size_t *, loff_t *);
int sysctl_min_unmapped_ratio_sysctl_handler(struct ctl_table *, int,
		void *, size_t *, loff_t *);
int sysctl_min_slab_ratio_sysctl_handler(struct ctl_table *, int,
		void *, size_t *, loff_t *);
int numa_zonelist_order_handler(struct ctl_table *, int,
		void *, size_t *, loff_t *);
extern int percpu_pagelist_high_fraction;
extern char numa_zonelist_order[];
#define NUMA_ZONELIST_ORDER_LEN	16


extern struct pglist_data contig_page_data;
static inline struct pglist_data *NODE_DATA(int nid)
{
	return &contig_page_data;
}


extern struct pglist_data *first_online_pgdat(void);
extern struct pglist_data *next_online_pgdat(struct pglist_data *pgdat);
extern struct zone *next_zone(struct zone *zone);

 
#define for_each_online_pgdat(pgdat)			\
	for (pgdat = first_online_pgdat();		\
	     pgdat;					\
	     pgdat = next_online_pgdat(pgdat))
 
#define for_each_zone(zone)			        \
	for (zone = (first_online_pgdat())->node_zones; \
	     zone;					\
	     zone = next_zone(zone))

#define for_each_populated_zone(zone)		        \
	for (zone = (first_online_pgdat())->node_zones; \
	     zone;					\
	     zone = next_zone(zone))			\
		if (!populated_zone(zone))		\
			;  		\
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

 
static __always_inline struct zoneref *next_zones_zonelist(struct zoneref *z,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes)
{
	if (likely(!nodes && zonelist_zone_idx(z) <= highest_zoneidx))
		return z;
	return __next_zones_zonelist(z, highest_zoneidx, nodes);
}

 
static inline struct zoneref *first_zones_zonelist(struct zonelist *zonelist,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes)
{
	return next_zones_zonelist(zonelist->_zonerefs,
							highest_zoneidx, nodes);
}

 
#define for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, nodemask) \
	for (z = first_zones_zonelist(zlist, highidx, nodemask), zone = zonelist_zone(z);	\
		zone;							\
		z = next_zones_zonelist(++z, highidx, nodemask),	\
			zone = zonelist_zone(z))

#define for_next_zone_zonelist_nodemask(zone, z, highidx, nodemask) \
	for (zone = z->zone;	\
		zone;							\
		z = next_zones_zonelist(++z, highidx, nodemask),	\
			zone = zonelist_zone(z))


 
#define for_each_zone_zonelist(zone, z, zlist, highidx) \
	for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, NULL)


#define pfn_to_nid(pfn)		(0)

#define sparse_init()	do {} while (0)
#define sparse_index_init(_sec, _nid)  do {} while (0)
#define pfn_in_present_section pfn_valid
#define subsection_map_init(_pfn, _nr_pages) do {} while (0)

#endif  
#endif  
#endif  
