
#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/memblock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/module.h>
/* suspend.h, pagevec.h removed - unused */
#include <linux/blkdev.h>
#include <linux/slab.h>
/* ratelimit.h removed - unused */
#include <linux/oom.h>
#include <linux/topology.h>
/* sysctl.h removed - unused */
#include <linux/cpu.h>
/* cpuset.h, memory_hotplug.h removed - unused */
#include <linux/nodemask.h>
/* vmalloc.h, vmstat.h, mempolicy.h, memremap.h removed - unused */
/* stop_machine.h removed - unused */
/* sort.h removed - unused */
#include <linux/pfn.h>
/* backing-dev.h removed - unused */

/* MEMORY_OFFLINE, REPORT_FAILURE removed - unused */

void set_pageblock_migratetype(struct page *page, int migratetype);
/* end page-isolation.h */

/* compact_priority, compact_result enums removed - never used */

struct alloc_context;

#include <linux/mm_inline.h>
/* mmu_notifier.h, linux/migrate.h removed - unused/empty */
/* hugetlb.h removed - unused */
#include <linux/sched/rt.h>
#include <linux/sched/mm.h>

/* Removed: reset_page_owner, set_page_owner, split_page_owner
 * - Dead code: page_owner tracking not needed for minimal kernel (~9 LOC) */
/* page_table_check.h, kthread.h, memcontrol.h removed - unused */
#include <linux/lockdep.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/div64.h>
#include "internal.h"
#define page_reported(_page) false
/* end page_reporting.h */
struct swap_iocb;

typedef int __bitwise fpi_t;
/* FPI_NONE removed - unused */
#define FPI_TO_TAIL ((__force fpi_t)BIT(1))
#define FPI_SKIP_KASAN_POISON ((__force fpi_t)BIT(2))
/* MIN_PERCPU_PAGELIST_HIGH_FRACTION removed - unused */

struct pagesets {
	local_lock_t lock;
};
static DEFINE_PER_CPU(struct pagesets, pagesets) = {
	.lock = INIT_LOCAL_LOCK(lock),
};

nodemask_t node_states[NR_NODE_STATES] __read_mostly = {
	[N_POSSIBLE] = NODE_MASK_ALL,	       [N_ONLINE] = { { [0] = 1UL } },
	[N_NORMAL_MEMORY] = { { [0] = 1UL } }, [N_MEMORY] = { { [0] = 1UL } },
	[N_CPU] = { { [0] = 1UL } },
};

atomic_long_t _totalram_pages __read_mostly;
unsigned long totalreserve_pages __read_mostly;
gfp_t gfp_allowed_mask __read_mostly = GFP_BOOT_MASK;
DEFINE_STATIC_KEY_MAYBE(CONFIG_INIT_ON_ALLOC_DEFAULT_ON, init_on_alloc);

DEFINE_STATIC_KEY_MAYBE(CONFIG_INIT_ON_FREE_DEFAULT_ON, init_on_free);

static inline int get_pcppage_migratetype(struct page *page)
{
	return page->index;
}

static void __free_pages_ok(struct page *page, unsigned int order,
			    fpi_t fpi_flags);

static char *const zone_names[MAX_NR_ZONES] = {
	"Normal",
	"Movable",
};

compound_page_dtor *const compound_page_dtors[NR_COMPOUND_DTORS] = {
	[NULL_COMPOUND_DTOR] = NULL,
	[COMPOUND_PAGE_DTOR] = free_compound_page,
};

/* min_free_kbytes, user_min_free_kbytes removed - unused */

static unsigned long arch_zone_lowest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long arch_zone_highest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long zone_movable_pfn[MAX_NUMNODES] __initdata;

int movable_zone;

/* MAX_NUMNODES == 1, removed unused nr_node_ids/nr_online_nodes */

int page_group_by_mobility_disabled __read_mostly;

/* defer_init removed - unused */

static inline unsigned long *get_pageblock_bitmap(const struct page *page,
						  unsigned long pfn)
{
	return page_zone(page)->pageblock_flags;
}

static inline int pfn_to_bitidx(const struct page *page, unsigned long pfn)
{
	pfn = pfn -
	      round_down(page_zone(page)->zone_start_pfn, pageblock_nr_pages);
	return (pfn >> pageblock_order) * NR_PAGEBLOCK_BITS;
}

static __always_inline unsigned long
__get_pfnblock_flags_mask(const struct page *page, unsigned long pfn,
			  unsigned long mask)
{
	unsigned long *bitmap;
	unsigned long bitidx, word_bitidx;
	unsigned long word;

	bitmap = get_pageblock_bitmap(page, pfn);
	bitidx = pfn_to_bitidx(page, pfn);
	word_bitidx = bitidx / BITS_PER_LONG;
	bitidx &= (BITS_PER_LONG - 1);

	word = READ_ONCE(bitmap[word_bitidx]);
	return (word >> bitidx) & mask;
}

unsigned long get_pfnblock_flags_mask(const struct page *page,
				      unsigned long pfn, unsigned long mask)
{
	return __get_pfnblock_flags_mask(page, pfn, mask);
}

void set_pfnblock_flags_mask(struct page *page, unsigned long flags,
			     unsigned long pfn, unsigned long mask)
{
	unsigned long *bitmap;
	unsigned long bitidx, word_bitidx;
	unsigned long old_word, word;

	BUILD_BUG_ON(NR_PAGEBLOCK_BITS != 4);
	BUILD_BUG_ON(MIGRATE_TYPES > (1 << PB_migratetype_bits));

	bitmap = get_pageblock_bitmap(page, pfn);
	bitidx = pfn_to_bitidx(page, pfn);
	word_bitidx = bitidx / BITS_PER_LONG;
	bitidx &= (BITS_PER_LONG - 1);

	mask <<= bitidx;
	flags <<= bitidx;

	word = READ_ONCE(bitmap[word_bitidx]);
	for (;;) {
		old_word = cmpxchg(&bitmap[word_bitidx], word,
				   (word & ~mask) | flags);
		if (word == old_word)
			break;
		word = old_word;
	}
}

void set_pageblock_migratetype(struct page *page, int migratetype)
{
	if (unlikely(page_group_by_mobility_disabled &&
		     migratetype < MIGRATE_PCPTYPES))
		migratetype = MIGRATE_UNMOVABLE;

	set_pfnblock_flags_mask(page, (unsigned long)migratetype,
				page_to_pfn(page), MIGRATETYPE_MASK);
}

/* bad_range removed - only used in VM_BUG_ON which are no-ops */

/* free_the_page removed - unused stub */

void free_compound_page(struct page *page)
{
	/* No-op: bump allocator style - no deallocation */
}

void prep_compound_page(struct page *page, unsigned int order)
{
	int i;
	int nr_pages = 1 << order;
	struct page *p;

	__SetPageHead(page);
	for (i = 1; i < nr_pages; i++) {
		p = page + i;
		p->mapping = TAIL_MAPPING;
		set_compound_head(p, page);
	}

	set_compound_page_dtor(page, COMPOUND_PAGE_DTOR);
	set_compound_order(page, order);
	atomic_set(compound_mapcount_ptr(page), -1);
	atomic_set(compound_pincount_ptr(page), 0);
}

void init_mem_debugging_and_hardening(void)
{
	/* Stub: memory debugging/hardening not needed for minimal kernel */
	static_branch_disable(&init_on_alloc);
	static_branch_disable(&init_on_free);
}

static inline void set_buddy_order(struct page *page, unsigned int order)
{
	set_page_private(page, order);
	__SetPageBuddy(page);
}

static inline void add_to_free_list(struct page *page, struct zone *zone,
				    unsigned int order, int migratetype)
{
	struct free_area *area = &zone->free_area[order];

	list_add(&page->lru, &area->free_list[migratetype]);
	area->nr_free++;
}

/* add_to_free_list_tail and move_to_free_list inlined into callers */

static inline void del_page_from_free_list(struct page *page, struct zone *zone,
					   unsigned int order)
{
	/* page_reported() always false - check removed */

	list_del(&page->lru);
	__ClearPageBuddy(page);
	set_page_private(page, 0);
	zone->free_area[order].nr_free--;
}

static inline void __free_one_page(struct page *page, unsigned long pfn,
				   struct zone *zone, unsigned int order,
				   int migratetype, fpi_t fpi_flags)
{
	/* Simplified buddy allocator: just add to free list without merging */
	__mod_zone_freepage_state(zone, 1 << order, migratetype);

	set_buddy_order(page, order);

	if (fpi_flags & FPI_TO_TAIL) {
		struct free_area *area = &zone->free_area[order];
		list_add_tail(&page->lru, &area->free_list[migratetype]);
		area->nr_free++;
	} else
		add_to_free_list(page, zone, order, migratetype);
}

static __always_inline bool free_pages_prepare(struct page *page,
					       unsigned int order,
					       bool check_free, fpi_t fpi_flags)
{
	/* Stub: minimal page freeing for simple system */
	if (PageMappingFlags(page))
		page->mapping = NULL;
	/* memcg_kmem_enabled always returns false */
	page->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
	return true;
}

/* Removed: free_pcp_prepare, free_pcppages_bulk
 * - Dead code since free_unref_page and free_the_page are no-ops (~15 lines) */

/* free_one_page removed - unused (callers use __free_one_page directly) */

static void __meminit __init_single_page(struct page *page, unsigned long pfn,
					 unsigned long zone, int nid)
{
	mm_zero_struct_page(page);
	set_page_links(page, zone, nid, pfn);
	init_page_count(page);
	page_mapcount_reset(page);
	/* page_cpupid_reset_last removed - empty stub */

	INIT_LIST_HEAD(&page->lru);
	/* WANT_PAGE_VIRTUAL not defined */
}

void __meminit reserve_bootmem_region(phys_addr_t start, phys_addr_t end)
{
	unsigned long start_pfn = PFN_DOWN(start);
	unsigned long end_pfn = PFN_UP(end);

	for (; start_pfn < end_pfn; start_pfn++) {
		if (pfn_valid(start_pfn)) {
			struct page *page = pfn_to_page(start_pfn);

			INIT_LIST_HEAD(&page->lru);

			__SetPageReserved(page);
		}
	}
}

static void __free_pages_ok(struct page *page, unsigned int order,
			    fpi_t fpi_flags)
{
	unsigned long flags;
	int migratetype;
	unsigned long pfn = page_to_pfn(page);
	struct zone *zone = page_zone(page);

	if (!free_pages_prepare(page, order, true, fpi_flags))
		return;

	migratetype = __get_pfnblock_flags_mask(page, pfn, MIGRATETYPE_MASK);

	spin_lock_irqsave(&zone->lock, flags);
	__free_one_page(page, pfn, zone, order, migratetype, fpi_flags);
	spin_unlock_irqrestore(&zone->lock, flags);
}

void __free_pages_core(struct page *page, unsigned int order)
{
	unsigned int nr_pages = 1 << order;
	struct page *p = page;
	unsigned int loop;

	prefetchw(p);
	for (loop = 0; loop < (nr_pages - 1); loop++, p++) {
		prefetchw(p + 1);
		__ClearPageReserved(p);
		set_page_count(p, 0);
	}
	__ClearPageReserved(p);
	set_page_count(p, 0);

	atomic_long_add(nr_pages, &page_zone(page)->managed_pages);

	__free_pages_ok(page, order, FPI_TO_TAIL | FPI_SKIP_KASAN_POISON);
}

void __init memblock_free_pages(struct page *page, unsigned long pfn,
				unsigned int order)
{
	__free_pages_core(page, order);
}

void __init page_alloc_init_late(void)
{
	memblock_discard();
	/* contiguous removed - write-only */
}

static inline void expand(struct zone *zone, struct page *page, int low,
			  int high, int migratetype)
{
	unsigned long size = 1 << high;

	while (high > low) {
		high--;
		size >>= 1;
		/* set_page_guard() always returns false - check removed */
		add_to_free_list(&page[size], zone, high, migratetype);
		set_buddy_order(&page[size], high);
	}
}

/* check_new_pages, check_pcp_refill, check_new_pcp always return false - removed */

inline void post_alloc_hook(struct page *page, unsigned int order,
			    gfp_t gfp_flags)
{
	/* Stub: minimal post-allocation setup */
	set_page_private(page, 0);
	set_page_refcounted(page);
}

/* prep_new_page inlined into get_page_from_freelist */

static __always_inline struct page *
__rmqueue_smallest(struct zone *zone, unsigned int order, int migratetype)
{
	unsigned int current_order;
	struct free_area *area;
	struct page *page;

	for (current_order = order; current_order < MAX_ORDER;
	     ++current_order) {
		area = &(zone->free_area[current_order]);
		page = get_page_from_free_area(area, migratetype);
		if (!page)
			continue;
		del_page_from_free_list(page, zone, current_order);
		expand(zone, page, order, current_order, migratetype);
		page->index = migratetype;
		return page;
	}

	return NULL;
}

static int fallbacks[MIGRATE_TYPES][3] = {
	[MIGRATE_UNMOVABLE] = { MIGRATE_RECLAIMABLE, MIGRATE_MOVABLE,
				MIGRATE_TYPES },
	[MIGRATE_MOVABLE] = { MIGRATE_RECLAIMABLE, MIGRATE_UNMOVABLE,
			      MIGRATE_TYPES },
	[MIGRATE_RECLAIMABLE] = { MIGRATE_UNMOVABLE, MIGRATE_MOVABLE,
				  MIGRATE_TYPES },
};

/* steal_suitable_fallback and find_suitable_fallback inlined into __rmqueue_fallback */

static __always_inline bool __rmqueue_fallback(struct zone *zone, int order,
					       int start_migratetype,
					       unsigned int alloc_flags)
{
	/* Simplified fallback: try all orders, take first match */
	struct free_area *area;
	int current_order;
	int fallback_mt;
	struct page *page;

	for (current_order = MAX_ORDER - 1; current_order >= order;
	     --current_order) {
		area = &(zone->free_area[current_order]);
		/* Inlined find_suitable_fallback */
		fallback_mt = -1;
		if (area->nr_free != 0) {
			int i;
			for (i = 0;; i++) {
				int mt = fallbacks[start_migratetype][i];
				if (mt == MIGRATE_TYPES)
					break;
				if (!free_area_empty(area, mt)) {
					fallback_mt = mt;
					break;
				}
			}
		}
		if (fallback_mt == -1)
			continue;

		page = get_page_from_free_area(area, fallback_mt);
		/* Inlined steal_suitable_fallback and move_to_free_list */
		list_move_tail(&page->lru,
			       &zone->free_area[buddy_order(page)]
					.free_list[start_migratetype]);
		return true;
	}

	return false;
}

static __always_inline struct page *__rmqueue(struct zone *zone,
					      unsigned int order,
					      int migratetype,
					      unsigned int alloc_flags)
{
	struct page *page;

	/* CONFIG_CMA not enabled - CMA block removed */
retry:
	page = __rmqueue_smallest(zone, order, migratetype);
	if (unlikely(!page) &&
	    __rmqueue_fallback(zone, order, migratetype, alloc_flags))
		goto retry;
	return page;
}

/* Removed: rmqueue_bulk inlined into __rmqueue_pcplist
 * free_unref_page_prepare, nr_pcp_free, nr_pcp_high, free_unref_page_commit, drain_pages
 * - Dead code since free_unref_page is now a no-op (~55 lines) */

void free_unref_page(struct page *page, unsigned int order)
{
	/* No-op: bump allocator style - no deallocation */
}

void free_unref_page_list(struct list_head *list)
{
	/* No-op: bump allocator style - no deallocation */
}

void split_page(struct page *page, unsigned int order)
{
	int i;
	for (i = 1; i < (1 << order); i++)
		set_page_refcounted(page + i);
}

/* Removed: zone_statistics - NUMA stats not needed */

static inline struct page *
__rmqueue_pcplist(struct zone *zone, unsigned int order, int migratetype,
		  unsigned int alloc_flags, struct per_cpu_pages *pcp,
		  struct list_head *list)
{
	struct page *page;

	if (list_empty(list)) {
		int batch = READ_ONCE(pcp->batch);
		int alloced = 0;

		if (batch > 1)
			batch = max(batch >> order, 2);
		{
			int i;
			spin_lock(&zone->lock);
			for (i = 0; i < batch; ++i) {
				struct page *pg = __rmqueue(
					zone, order, migratetype, alloc_flags);
				if (unlikely(pg == NULL))
					break;
				list_add_tail(&pg->lru, list);
				alloced++;
				if (is_migrate_cma(get_pcppage_migratetype(pg)))
					__mod_zone_page_state(zone,
							      NR_FREE_CMA_PAGES,
							      -(1 << order));
			}
			__mod_zone_page_state(zone, NR_FREE_PAGES,
					      -(i << order));
			spin_unlock(&zone->lock);
		}

		pcp->count += alloced << order;
		if (unlikely(list_empty(list)))
			return NULL;
	}

	page = list_first_entry(list, struct page, lru);
	list_del(&page->lru);
	pcp->count -= 1 << order;
	/* check_new_pcp always returns false - loop removed */
	return page;
}

static struct page *rmqueue_pcplist(struct zone *preferred_zone,
				    struct zone *zone, unsigned int order,
				    gfp_t gfp_flags, int migratetype,
				    unsigned int alloc_flags)
{
	struct per_cpu_pages *pcp;
	struct list_head *list;
	struct page *page;
	unsigned long flags;

	/* Stub: simplified PCP allocation for minimal kernel */
	local_lock_irqsave(&pagesets.lock, flags);
	pcp = this_cpu_ptr(zone->per_cpu_pageset);
	list = &pcp->lists[(MIGRATE_PCPTYPES * order) + migratetype];
	page = __rmqueue_pcplist(zone, order, migratetype, alloc_flags, pcp,
				 list);
	local_unlock_irqrestore(&pagesets.lock, flags);
	return page;
}

static inline struct page *rmqueue(struct zone *preferred_zone,
				   struct zone *zone, unsigned int order,
				   gfp_t gfp_flags, unsigned int alloc_flags,
				   int migratetype)
{
	unsigned long flags;
	struct page *page;

	if (likely(order <= PAGE_ALLOC_COSTLY_ORDER)) {
		/* CONFIG_CMA not enabled - always take this path */
		page = rmqueue_pcplist(preferred_zone, zone, order, gfp_flags,
				       migratetype, alloc_flags);
		goto out;
	}

	WARN_ON_ONCE((gfp_flags & __GFP_NOFAIL) && (order > 1));

	page = NULL;
	spin_lock_irqsave(&zone->lock, flags);

	if (order > 0 && alloc_flags & ALLOC_HARDER)
		page = __rmqueue_smallest(zone, order, MIGRATE_HIGHATOMIC);
	if (!page) {
		page = __rmqueue(zone, order, migratetype, alloc_flags);
		if (!page)
			goto failed;
	}
	__mod_zone_freepage_state(zone, -(1 << order),
				  get_pcppage_migratetype(page));
	spin_unlock_irqrestore(&zone->lock, flags);
	/* check_new_pages always returns false - loop removed */
out:
	return page;

failed:
	spin_unlock_irqrestore(&zone->lock, flags);
	return NULL;
}

bool __zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
			 int highest_zoneidx, unsigned int alloc_flags,
			 long free_pages)
{
	/* Simplified watermark check for minimal kernel */
	long min = mark;

	/* Apply alloc_flags adjustments */
	if (alloc_flags & ALLOC_HIGH)
		min -= min / 2;
	if (alloc_flags & (ALLOC_HARDER | ALLOC_OOM))
		min -= min / 2;

	/* Basic free pages check */
	return free_pages > min + z->lowmem_reserve[highest_zoneidx];
}

/* zone_watermark_fast inlined into get_page_from_freelist */
/* alloc_flags_nofragment inlined into __alloc_pages */

static struct page *get_page_from_freelist(gfp_t gfp_mask, unsigned int order,
					   int alloc_flags,
					   const struct alloc_context *ac)
{
	/* Minimal stub: simplified zone iteration */
	struct zoneref *z;
	struct zone *zone;
	struct page *page;

	z = ac->preferred_zoneref;
	for_next_zone_zonelist_nodemask(zone, z, ac->highest_zoneidx,
					ac->nodemask)
	{
		unsigned long mark =
			wmark_pages(zone, alloc_flags & ALLOC_WMARK_MASK);

		/* Skip watermark check if NO_WATERMARKS flag set */
		/* Inlined zone_watermark_fast */
		if (!(alloc_flags & ALLOC_NO_WATERMARKS) &&
		    !__zone_watermark_ok(zone, order, mark, ac->highest_zoneidx,
					 alloc_flags,
					 zone_page_state(zone, NR_FREE_PAGES)))
			continue;

		page = rmqueue(ac->preferred_zoneref->zone, zone, order,
			       gfp_mask, alloc_flags, ac->migratetype);
		if (page) {
			post_alloc_hook(page, order, gfp_mask);
			if (order && (gfp_mask & __GFP_COMP))
				prep_compound_page(page, order);
			if (alloc_flags & ALLOC_NO_WATERMARKS)
				set_page_pfmemalloc(page);
			else
				clear_page_pfmemalloc(page);
			return page;
		}
	}

	return NULL;
}

void warn_alloc(gfp_t gfp_mask, nodemask_t *nodemask, const char *fmt, ...)
{
	/* Stub: allocation warning not needed for minimal kernel */
}

bool gfp_pfmemalloc_allowed(gfp_t gfp_mask)
{
	/* Inlined __gfp_pfmemalloc_flags */
	if (unlikely(gfp_mask & __GFP_NOMEMALLOC))
		return false;
	if (gfp_mask & __GFP_MEMALLOC)
		return true;
	if (in_serving_softirq() && (current->flags & PF_MEMALLOC))
		return true;
	if (!in_interrupt() && (current->flags & PF_MEMALLOC))
		return true;
	return false;
}

static inline struct page *__alloc_pages_slowpath(gfp_t gfp_mask,
						  unsigned int order,
						  struct alloc_context *ac)
{
	/* Minimal stub: skip complex OOM/reclaim/compaction logic */
	struct page *page;
	/* Inlined gfp_to_alloc_flags */
	unsigned int alloc_flags = ALLOC_WMARK_MIN | ALLOC_CPUSET;

	BUILD_BUG_ON(__GFP_HIGH != (__force gfp_t)ALLOC_HIGH);
	BUILD_BUG_ON(__GFP_KSWAPD_RECLAIM != (__force gfp_t)ALLOC_KSWAPD);

	alloc_flags |=
		(__force int)(gfp_mask & (__GFP_HIGH | __GFP_KSWAPD_RECLAIM));

	if (gfp_mask & __GFP_ATOMIC) {
		if (!(gfp_mask & __GFP_NOMEMALLOC))
			alloc_flags |= ALLOC_HARDER;

		alloc_flags &= ~ALLOC_CPUSET;
	} else if (unlikely(rt_task(current)) && in_task())
		alloc_flags |= ALLOC_HARDER;

	/* Try basic allocation once */
	ac->preferred_zoneref = first_zones_zonelist(
		ac->zonelist, ac->highest_zoneidx, ac->nodemask);
	if (!ac->preferred_zoneref->zone)
		return NULL;

	page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);
	return page;
}

static inline bool prepare_alloc_pages(gfp_t gfp_mask, unsigned int order,
				       int preferred_nid, nodemask_t *nodemask,
				       struct alloc_context *ac,
				       gfp_t *alloc_gfp,
				       unsigned int *alloc_flags)
{
	ac->highest_zoneidx = gfp_zone(gfp_mask);
	ac->zonelist = node_zonelist(preferred_nid, gfp_mask);
	ac->nodemask = nodemask;
	ac->migratetype = gfp_migratetype(gfp_mask);

	/* fs_reclaim_acquire/release removed - empty stubs */

	might_sleep_if(gfp_mask & __GFP_DIRECT_RECLAIM);

	ac->spread_dirty_pages = (gfp_mask & __GFP_WRITE);

	ac->preferred_zoneref = first_zones_zonelist(
		ac->zonelist, ac->highest_zoneidx, ac->nodemask);

	return true;
}

unsigned long __alloc_pages_bulk(gfp_t gfp, int preferred_nid,
				 nodemask_t *nodemask, int nr_pages,
				 struct list_head *page_list,
				 struct page **page_array)
{
	struct page *page;
	int nr_populated = 0;

	/* Minimal stub: just allocate pages one at a time */
	while (nr_populated < nr_pages) {
		page = __alloc_pages(gfp, 0, preferred_nid, nodemask);
		if (!page)
			break;
		if (page_list)
			list_add(&page->lru, page_list);
		else
			page_array[nr_populated] = page;
		nr_populated++;
	}
	return nr_populated;
}

struct page *__alloc_pages(gfp_t gfp, unsigned int order, int preferred_nid,
			   nodemask_t *nodemask)
{
	struct page *page;
	unsigned int alloc_flags = ALLOC_WMARK_LOW;
	gfp_t alloc_gfp;
	struct alloc_context ac = {};

	if (WARN_ON_ONCE_GFP(order >= MAX_ORDER, gfp))
		return NULL;

	gfp &= gfp_allowed_mask;

	gfp = current_gfp_context(gfp);
	alloc_gfp = gfp;
	if (!prepare_alloc_pages(gfp, order, preferred_nid, nodemask, &ac,
				 &alloc_gfp, &alloc_flags))
		return NULL;

	/* Inlined alloc_flags_nofragment */
	alloc_flags |= (__force int)(gfp & __GFP_KSWAPD_RECLAIM);

	page = get_page_from_freelist(alloc_gfp, order, alloc_flags, &ac);
	if (likely(page))
		goto out;

	alloc_gfp = gfp;
	ac.spread_dirty_pages = false;

	ac.nodemask = nodemask;

	page = __alloc_pages_slowpath(alloc_gfp, order, &ac);

out:
	/* memcg_kmem_enabled always returns false */
	return page;
}

struct folio *__folio_alloc(gfp_t gfp, unsigned int order, int preferred_nid,
			    nodemask_t *nodemask)
{
	struct page *page =
		__alloc_pages(gfp | __GFP_COMP, order, preferred_nid, nodemask);
	return (struct folio *)page;
}

unsigned long __get_free_pages(gfp_t gfp_mask, unsigned int order)
{
	struct page *page;

	page = alloc_pages(gfp_mask & ~__GFP_HIGHMEM, order);
	if (!page)
		return 0;
	return (unsigned long)page_address(page);
}
/* get_zeroed_page removed - never called */

void __free_pages(struct page *page, unsigned int order)
{
	/* No-op: bump allocator style - no deallocation */
}

void free_pages(unsigned long addr, unsigned int order)
{
	/* No-op: bump allocator style - no deallocation */
}

void *alloc_pages_exact(size_t size, gfp_t gfp_mask)
{
	unsigned int order = get_order(size);
	unsigned long addr;

	if (WARN_ON_ONCE(gfp_mask & (__GFP_COMP | __GFP_HIGHMEM)))
		gfp_mask &= ~(__GFP_COMP | __GFP_HIGHMEM);

	addr = __get_free_pages(gfp_mask, order);
	/* Inlined make_alloc_exact */
	if (addr) {
		unsigned long alloc_end = addr + (PAGE_SIZE << order);
		unsigned long used = addr + PAGE_ALIGN(size);

		split_page(virt_to_page((void *)addr), order);
		while (used < alloc_end) {
			free_page(used);
			used += PAGE_SIZE;
		}
	}
	return (void *)addr;
}

static unsigned long nr_free_zone_pages(int offset)
{
	struct zoneref *z;
	struct zone *zone;
	unsigned long sum = 0;
	struct zonelist *zonelist = node_zonelist(numa_node_id(), GFP_KERNEL);

	for_each_zone_zonelist(zone, z, zonelist, offset) {
		unsigned long size = zone_managed_pages(zone);
		unsigned long high = high_wmark_pages(zone);
		if (size > high)
			sum += size - high;
	}
	return sum;
}

/* nr_free_buffer_pages removed - never called */

static void build_zonelists(pg_data_t *pgdat)
{
	struct zoneref *zonerefs;
	struct zone *zone;
	enum zone_type zone_type = MAX_NR_ZONES;
	int nr_zones = 0;

	/* Simplified: single-node system, just build local node zonelist */
	zonerefs = pgdat->node_zonelists[ZONELIST_FALLBACK]._zonerefs;
	do {
		zone_type--;
		zone = pgdat->node_zones + zone_type;
		if (populated_zone(zone)) {
			zonerefs[nr_zones].zone = zone;
			zonerefs[nr_zones].zone_idx = zone_idx(zone);
			nr_zones++;
		}
	} while (zone_type);
	zonerefs += nr_zones;

	zonerefs->zone = NULL;
	zonerefs->zone_idx = 0;
}

static void per_cpu_pages_init(struct per_cpu_pages *pcp,
			       struct per_cpu_zonestat *pzstats);

#define BOOT_PAGESET_HIGH 0
#define BOOT_PAGESET_BATCH 1
static DEFINE_PER_CPU(struct per_cpu_pages, boot_pageset);
static DEFINE_PER_CPU(struct per_cpu_zonestat, boot_zonestats);
DEFINE_PER_CPU(struct per_cpu_nodestat, boot_nodestats);

static void __build_all_zonelists(void *data)
{
	/* nid, cpu variables removed - unused in single-node/CPU config */
	pg_data_t *self = data;
	static DEFINE_SPINLOCK(lock);

	spin_lock(&lock);

	if (self && !node_online(self->node_id)) {
		build_zonelists(self);
	} else {
		/* for_each_node simplified - single node */
		build_zonelists(NODE_DATA(0));
	}

	spin_unlock(&lock);
}

static noinline void __init build_all_zonelists_init(void)
{
	/* cpu variable removed - only single CPU used */
	__build_all_zonelists(NULL);

	/* for_each_possible_cpu simplified - single CPU */
	per_cpu_pages_init(&per_cpu(boot_pageset, 0),
			   &per_cpu(boot_zonestats, 0));

	mminit_verify_zonelist();
}

void __ref build_all_zonelists(pg_data_t *pgdat)
{
	unsigned long vm_total_pages;

	if (system_state == SYSTEM_BOOTING) {
		build_all_zonelists_init();
	} else {
		__build_all_zonelists(pgdat);
	}

	vm_total_pages = nr_free_zone_pages(gfp_zone(GFP_HIGHUSER_MOVABLE));

	if (vm_total_pages < (pageblock_nr_pages * MIGRATE_TYPES))
		page_group_by_mobility_disabled = 1;
	else
		page_group_by_mobility_disabled = 0;
}

/* overlap_memmap_init removed - stub returning false, call site removed */

static void __meminit memmap_init_range(unsigned long size, int nid,
					unsigned long zone,
					unsigned long start_pfn,
					unsigned long zone_end_pfn,
					enum meminit_context context,
					struct vmem_altmap *altmap,
					int migratetype)
{
	unsigned long pfn, end_pfn = start_pfn + size;
	struct page *page;

	if (highest_memmap_pfn < end_pfn - 1)
		highest_memmap_pfn = end_pfn - 1;

	for (pfn = start_pfn; pfn < end_pfn;) {
		/* overlap_memmap_init call removed - stub always returned false */

		page = pfn_to_page(pfn);
		__init_single_page(page, pfn, zone, nid);
		if (context == MEMINIT_HOTPLUG)
			__SetPageReserved(page);

		if (IS_ALIGNED(pfn, pageblock_nr_pages)) {
			set_pageblock_migratetype(page, migratetype);
			cond_resched();
		}
		pfn++;
	}
}

static void __meminit zone_init_free_lists(struct zone *zone)
{
	unsigned int order, t;
	for_each_migratetype_order(order, t) {
		INIT_LIST_HEAD(&zone->free_area[order].free_list[t]);
		zone->free_area[order].nr_free = 0;
	}
}

static void __init init_unavailable_range(unsigned long spfn,
					  unsigned long epfn, int zone,
					  int node)
{
	unsigned long pfn;
	/* pgcnt removed - write-only counter, never read */

	for (pfn = spfn; pfn < epfn; pfn++) {
		if (!pfn_valid(ALIGN_DOWN(pfn, pageblock_nr_pages))) {
			pfn = ALIGN_DOWN(pfn, pageblock_nr_pages) +
			      pageblock_nr_pages - 1;
			continue;
		}
		__init_single_page(pfn_to_page(pfn), pfn, zone, node);
		__SetPageReserved(pfn_to_page(pfn));
	}
}

static void __init memmap_init_zone_range(struct zone *zone,
					  unsigned long start_pfn,
					  unsigned long end_pfn,
					  unsigned long *hole_pfn)
{
	unsigned long zone_start_pfn = zone->zone_start_pfn;
	unsigned long zone_end_pfn = zone_start_pfn + zone->spanned_pages;
	int nid = zone_to_nid(zone), zone_id = zone_idx(zone);

	start_pfn = clamp(start_pfn, zone_start_pfn, zone_end_pfn);
	end_pfn = clamp(end_pfn, zone_start_pfn, zone_end_pfn);

	if (start_pfn >= end_pfn)
		return;

	memmap_init_range(end_pfn - start_pfn, nid, zone_id, start_pfn,
			  zone_end_pfn, MEMINIT_EARLY, NULL, MIGRATE_MOVABLE);

	if (*hole_pfn < start_pfn)
		init_unavailable_range(*hole_pfn, start_pfn, zone_id, nid);

	*hole_pfn = end_pfn;
}

static void __init memmap_init(void)
{
	unsigned long start_pfn, end_pfn;
	unsigned long hole_pfn = 0;
	int i, j, zone_id = 0, nid;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid) {
		struct pglist_data *node = NODE_DATA(nid);

		for (j = 0; j < MAX_NR_ZONES; j++) {
			struct zone *zone = node->node_zones + j;

			if (!populated_zone(zone))
				continue;

			memmap_init_zone_range(zone, start_pfn, end_pfn,
					       &hole_pfn);
			zone_id = j;
		}
	}

	init_unavailable_range(hole_pfn, end_pfn, zone_id, nid);
}

void __init *memmap_alloc(phys_addr_t size, phys_addr_t align,
			  phys_addr_t min_addr, int nid, bool exact_nid)
{
	void *ptr;

	if (exact_nid)
		ptr = memblock_alloc_exact_nid_raw(
			size, align, min_addr, MEMBLOCK_ALLOC_ACCESSIBLE, nid);
	else
		ptr = memblock_alloc_try_nid_raw(
			size, align, min_addr, MEMBLOCK_ALLOC_ACCESSIBLE, nid);

	return ptr;
}

static void per_cpu_pages_init(struct per_cpu_pages *pcp,
			       struct per_cpu_zonestat *pzstats)
{
	int pindex;

	memset(pcp, 0, sizeof(*pcp));
	memset(pzstats, 0, sizeof(*pzstats));

	for (pindex = 0; pindex < NR_PCP_LISTS; pindex++)
		INIT_LIST_HEAD(&pcp->lists[pindex]);

	pcp->high = BOOT_PAGESET_HIGH;
	pcp->batch = BOOT_PAGESET_BATCH;
}

static void __zone_set_pageset_high_and_batch(struct zone *zone,
					      unsigned long high,
					      unsigned long batch)
{
	struct per_cpu_pages *pcp;
	/* for_each_possible_cpu simplified - single CPU */
	pcp = per_cpu_ptr(zone->per_cpu_pageset, 0);
	WRITE_ONCE(pcp->batch, batch);
	WRITE_ONCE(pcp->high, high);
}

static void zone_set_pageset_high_and_batch(struct zone *zone)
{
	/* Simplified: batch=1, high=4 for minimal kernel */
	__zone_set_pageset_high_and_batch(zone, 4, 1);
}

void __meminit setup_zone_pageset(struct zone *zone)
{
	if (sizeof(struct per_cpu_zonestat) > 0)
		zone->per_cpu_zonestats = alloc_percpu(struct per_cpu_zonestat);

	zone->per_cpu_pageset = alloc_percpu(struct per_cpu_pages);
	/* for_each_possible_cpu simplified - single CPU */
	{
		struct per_cpu_pages *pcp =
			per_cpu_ptr(zone->per_cpu_pageset, 0);
		struct per_cpu_zonestat *pzstats =
			per_cpu_ptr(zone->per_cpu_zonestats, 0);
		per_cpu_pages_init(pcp, pzstats);
	}

	zone_set_pageset_high_and_batch(zone);
}

void __init setup_per_cpu_pageset(void)
{
	struct pglist_data *pgdat;
	struct zone *zone;
	/* cpu variable removed - unused in single-CPU config */

	for_each_populated_zone(zone)
		setup_zone_pageset(zone);

	for_each_online_pgdat(pgdat)
		pgdat->per_cpu_nodestats =
			alloc_percpu(struct per_cpu_nodestat);
}

static __meminit void zone_pcp_init(struct zone *zone)
{
	zone->per_cpu_pageset = &boot_pageset;
	zone->per_cpu_zonestats = &boot_zonestats;
	/* pageset_high, pageset_batch fields removed */
}

void __meminit init_currently_empty_zone(struct zone *zone,
					 unsigned long zone_start_pfn,
					 unsigned long size)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	int zone_idx = zone_idx(zone) + 1;

	if (zone_idx > pgdat->nr_zones)
		pgdat->nr_zones = zone_idx;

	zone->zone_start_pfn = zone_start_pfn;

	mminit_dprintk(MMINIT_TRACE, "memmap_init",
		       "Initialising map node %d zone %lu pfns %lu -> %lu\n",
		       pgdat->node_id, (unsigned long)zone_idx(zone),
		       zone_start_pfn, (zone_start_pfn + size));

	zone_init_free_lists(zone);
}

static void __init get_pfn_range_for_nid(unsigned int nid,
					 unsigned long *start_pfn,
					 unsigned long *end_pfn)
{
	unsigned long this_start_pfn, this_end_pfn;
	int i;

	*start_pfn = -1UL;
	*end_pfn = 0;

	for_each_mem_pfn_range(i, nid, &this_start_pfn, &this_end_pfn, NULL) {
		*start_pfn = min(*start_pfn, this_start_pfn);
		*end_pfn = max(*end_pfn, this_end_pfn);
	}

	if (*start_pfn == -1UL)
		*start_pfn = 0;
}

static void __init adjust_zone_range_for_zone_movable(
	int nid, unsigned long zone_type, unsigned long node_start_pfn,
	unsigned long node_end_pfn, unsigned long *zone_start_pfn,
	unsigned long *zone_end_pfn)
{
	if (zone_movable_pfn[nid]) {
		if (zone_type == ZONE_MOVABLE) {
			*zone_start_pfn = zone_movable_pfn[nid];
			*zone_end_pfn = min(
				node_end_pfn,
				arch_zone_highest_possible_pfn[movable_zone]);

		} else if (*zone_start_pfn < zone_movable_pfn[nid] &&
			   *zone_end_pfn > zone_movable_pfn[nid]) {
			*zone_end_pfn = zone_movable_pfn[nid];

		} else if (*zone_start_pfn >= zone_movable_pfn[nid])
			*zone_start_pfn = *zone_end_pfn;
	}
}

static unsigned long __init zone_spanned_pages_in_node(
	int nid, unsigned long zone_type, unsigned long node_start_pfn,
	unsigned long node_end_pfn, unsigned long *zone_start_pfn,
	unsigned long *zone_end_pfn)
{
	unsigned long zone_low = arch_zone_lowest_possible_pfn[zone_type];
	unsigned long zone_high = arch_zone_highest_possible_pfn[zone_type];

	if (!node_start_pfn && !node_end_pfn)
		return 0;

	*zone_start_pfn = clamp(node_start_pfn, zone_low, zone_high);
	*zone_end_pfn = clamp(node_end_pfn, zone_low, zone_high);
	adjust_zone_range_for_zone_movable(nid, zone_type, node_start_pfn,
					   node_end_pfn, zone_start_pfn,
					   zone_end_pfn);

	if (*zone_end_pfn < node_start_pfn || *zone_start_pfn > node_end_pfn)
		return 0;

	*zone_end_pfn = min(*zone_end_pfn, node_end_pfn);
	*zone_start_pfn = max(*zone_start_pfn, node_start_pfn);

	return *zone_end_pfn - *zone_start_pfn;
}

static unsigned long __init __absent_pages_in_range(
	int nid, unsigned long range_start_pfn, unsigned long range_end_pfn)
{
	unsigned long nr_absent = range_end_pfn - range_start_pfn;
	unsigned long start_pfn, end_pfn;
	int i;

	for_each_mem_pfn_range(i, nid, &start_pfn, &end_pfn, NULL) {
		start_pfn = clamp(start_pfn, range_start_pfn, range_end_pfn);
		end_pfn = clamp(end_pfn, range_start_pfn, range_end_pfn);
		nr_absent -= end_pfn - start_pfn;
	}
	return nr_absent;
}

static unsigned long __init zone_absent_pages_in_node(
	int nid, unsigned long zone_type, unsigned long node_start_pfn,
	unsigned long node_end_pfn)
{
	unsigned long zone_low = arch_zone_lowest_possible_pfn[zone_type];
	unsigned long zone_high = arch_zone_highest_possible_pfn[zone_type];
	unsigned long zone_start_pfn, zone_end_pfn;
	unsigned long nr_absent;

	if (!node_start_pfn && !node_end_pfn)
		return 0;

	zone_start_pfn = clamp(node_start_pfn, zone_low, zone_high);
	zone_end_pfn = clamp(node_end_pfn, zone_low, zone_high);

	adjust_zone_range_for_zone_movable(nid, zone_type, node_start_pfn,
					   node_end_pfn, &zone_start_pfn,
					   &zone_end_pfn);
	nr_absent = __absent_pages_in_range(nid, zone_start_pfn, zone_end_pfn);

	return nr_absent;
}

static void __init calculate_node_totalpages(struct pglist_data *pgdat,
					     unsigned long node_start_pfn,
					     unsigned long node_end_pfn)
{
	unsigned long realtotalpages = 0, totalpages = 0;
	enum zone_type i;

	for (i = 0; i < MAX_NR_ZONES; i++) {
		struct zone *zone = pgdat->node_zones + i;
		unsigned long zone_start_pfn, zone_end_pfn;
		unsigned long spanned, absent;
		unsigned long size, real_size;

		spanned = zone_spanned_pages_in_node(
			pgdat->node_id, i, node_start_pfn, node_end_pfn,
			&zone_start_pfn, &zone_end_pfn);
		absent = zone_absent_pages_in_node(
			pgdat->node_id, i, node_start_pfn, node_end_pfn);

		size = spanned;
		real_size = size - absent;

		if (size)
			zone->zone_start_pfn = zone_start_pfn;
		else
			zone->zone_start_pfn = 0;
		zone->spanned_pages = size;
		zone->present_pages = real_size;

		totalpages += size;
		realtotalpages += real_size;
	}

	pgdat->node_spanned_pages = totalpages;
	pgdat->node_present_pages = realtotalpages;
}

static unsigned long __init usemap_size(unsigned long zone_start_pfn,
					unsigned long zonesize)
{
	unsigned long usemapsize;

	zonesize += zone_start_pfn & (pageblock_nr_pages - 1);
	usemapsize = roundup(zonesize, pageblock_nr_pages);
	usemapsize = usemapsize >> pageblock_order;
	usemapsize *= NR_PAGEBLOCK_BITS;
	usemapsize = roundup(usemapsize, 8 * sizeof(unsigned long));

	return usemapsize / 8;
}

static void __ref setup_usemap(struct zone *zone)
{
	unsigned long usemapsize =
		usemap_size(zone->zone_start_pfn, zone->spanned_pages);
	zone->pageblock_flags = NULL;
	if (usemapsize) {
		zone->pageblock_flags = memblock_alloc_node(
			usemapsize, SMP_CACHE_BYTES, zone_to_nid(zone));
		if (!zone->pageblock_flags)
			panic("Failed to allocate %ld bytes for zone %s pageblock flags on node %d\n",
			      usemapsize, zone->name, zone_to_nid(zone));
	}
}

void __init set_pageblock_order(void)
{
}

static unsigned long __init calc_memmap_size(unsigned long spanned_pages,
					     unsigned long present_pages)
{
	/* SPARSEMEM disabled */
	return PAGE_ALIGN(spanned_pages * sizeof(struct page)) >> PAGE_SHIFT;
}

static void __init free_area_init_core(struct pglist_data *pgdat)
{
	enum zone_type j;
	int nid = pgdat->node_id;

	/* Inlined pgdat_init_internals */
	init_waitqueue_head(&pgdat->kswapd_wait);
	init_waitqueue_head(&pgdat->pfmemalloc_wait);
	/* reclaim_wait init removed - field removed */
	lruvec_init(&pgdat->__lruvec);

	pgdat->per_cpu_nodestats = &boot_nodestats;

	for (j = 0; j < MAX_NR_ZONES; j++) {
		struct zone *zone = pgdat->node_zones + j;
		unsigned long size, freesize, memmap_pages;

		size = zone->spanned_pages;
		freesize = zone->present_pages;

		memmap_pages = calc_memmap_size(size, freesize);
		/* is_highmem_idx always returns 0, so !is_highmem_idx is always true */
		if (freesize >= memmap_pages)
			freesize -= memmap_pages;

		/* dma_reserve, nr_kernel_pages, nr_all_pages removed - never read */

		/* Inlined zone_init_internals */
		atomic_long_set(&zone->managed_pages, freesize);
		zone->name = zone_names[j];
		zone->zone_pgdat = NODE_DATA(nid);
		spin_lock_init(&zone->lock);
		zone_pcp_init(zone);

		if (!size)
			continue;

		set_pageblock_order();
		setup_usemap(zone);
		init_currently_empty_zone(zone, zone->zone_start_pfn, size);
	}
}

static void __init alloc_node_mem_map(struct pglist_data *pgdat)
{
	unsigned long start = 0;
	unsigned long offset = 0;

	if (!pgdat->node_spanned_pages)
		return;

	start = pgdat->node_start_pfn & ~(MAX_ORDER_NR_PAGES - 1);
	offset = pgdat->node_start_pfn - start;

	if (!pgdat->node_mem_map) {
		unsigned long size, end;
		struct page *map;

		end = pgdat_end_pfn(pgdat);
		end = ALIGN(end, MAX_ORDER_NR_PAGES);
		size = (end - start) * sizeof(struct page);
		map = memmap_alloc(size, SMP_CACHE_BYTES, MEMBLOCK_LOW_LIMIT,
				   pgdat->node_id, false);
		if (!map)
			panic("Failed to allocate %ld bytes for node %d memory map\n",
			      size, pgdat->node_id);
		pgdat->node_mem_map = map + offset;
	}

	if (pgdat == NODE_DATA(0)) {
		mem_map = NODE_DATA(0)->node_mem_map;
		if (page_to_pfn(mem_map) != pgdat->node_start_pfn)
			mem_map -= offset;
	}
}

static void __init free_area_init_node(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	unsigned long start_pfn = 0;
	unsigned long end_pfn = 0;

	WARN_ON(pgdat->nr_zones || pgdat->kswapd_highest_zoneidx);

	get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);

	pgdat->node_id = nid;
	pgdat->node_start_pfn = start_pfn;
	pgdat->per_cpu_nodestats = NULL;

	calculate_node_totalpages(pgdat, start_pfn, end_pfn);

	alloc_node_mem_map(pgdat);
	free_area_init_core(pgdat);
}

/* MAX_NUMNODES == 1, setup_nr_node_ids removed - inline stub in mm.h */

void __init free_area_init(unsigned long *max_zone_pfn)
{
	unsigned long start_pfn, end_pfn;
	int i;
	/* nid removed - single node config uses hardcoded 0 */

	/* Minimal zone setup - just set up basic pfn ranges */
	memset(arch_zone_lowest_possible_pfn, 0,
	       sizeof(arch_zone_lowest_possible_pfn));
	memset(arch_zone_highest_possible_pfn, 0,
	       sizeof(arch_zone_highest_possible_pfn));

	/* Inlined find_min_pfn_with_active_regions */
	start_pfn = PHYS_PFN(memblock_start_of_DRAM());

	/* Simple linear zone setup without movable zones */
	for (i = 0; i < MAX_NR_ZONES; i++) {
		if (i == ZONE_MOVABLE)
			continue;
		end_pfn = max(max_zone_pfn[i], start_pfn);
		arch_zone_lowest_possible_pfn[i] = start_pfn;
		arch_zone_highest_possible_pfn[i] = end_pfn;
		start_pfn = end_pfn;
	}

	/* Basic node initialization - single node */
	if (node_online(0))
		free_area_init_node(0);

	memmap_init();
}

void adjust_managed_page_count(struct page *page, long count)
{
	atomic_long_add(count, &page_zone(page)->managed_pages);
	totalram_pages_add(count);
}

unsigned long free_reserved_area(void *start, void *end, int poison,
				 const char *s)
{
	void *pos;
	unsigned long pages = 0;

	/* Simplified: skip poisoning for minimal system */
	start = (void *)PAGE_ALIGN((unsigned long)start);
	end = (void *)((unsigned long)end & PAGE_MASK);
	for (pos = start; pos < end; pos += PAGE_SIZE, pages++) {
		free_reserved_page(virt_to_page(pos));
	}

	return pages;
}

/* mem_init_print_info removed - empty stub */

static int page_alloc_cpu_dead(unsigned int cpu)
{
	lru_add_drain_cpu(cpu);
	/* vm_events_fold_cpu, cpu_vm_stats_fold, zone_pcp_update removed - stubs or unused */
	return 0;
}

static int page_alloc_cpu_online(unsigned int cpu)
{
	/* zone_pcp_update removed - not needed for single CPU minimal kernel */
	return 0;
}

void __init page_alloc_init(void)
{
	int ret;

	ret = cpuhp_setup_state_nocalls(CPUHP_PAGE_ALLOC, "mm/page_alloc:pcp",
					page_alloc_cpu_online,
					page_alloc_cpu_dead);
	WARN_ON(ret < 0);
}

/* calculate_totalreserve_pages, setup_per_zone_lowmem_reserve, __setup_per_zone_wmarks,
   setup_per_zone_wmarks, calculate_min_free_kbytes removed - unused */

int __meminit init_per_zone_wmark_min(void)
{
	/* Simplified for 4MB boot */
	return 0;
}
postcore_initcall(init_per_zone_wmark_min)

	/* ADAPT_SCALE_* removed - unused 64-bit only code */

	void *__init
	alloc_large_system_hash(const char *tablename, unsigned long bucketsize,
				unsigned long numentries, int scale, int flags,
				unsigned int *_hash_shift,
				unsigned int *_hash_mask,
				unsigned long low_limit,
				unsigned long high_limit)
{
	unsigned long log2qty, size;
	void *table;
	gfp_t gfp_flags;

	/* Minimal stub: simple hash table allocation */
	if (!numentries)
		numentries = 256; /* Small default */
	numentries = roundup_pow_of_two(numentries);
	log2qty = ilog2(numentries);
	size = bucketsize << log2qty;

	gfp_flags = (flags & HASH_ZERO) ? GFP_ATOMIC | __GFP_ZERO : GFP_ATOMIC;
	if (flags & HASH_EARLY)
		table = memblock_alloc(size, SMP_CACHE_BYTES);
	else
		table = alloc_pages_exact(size, gfp_flags);

	if (!table)
		panic("Failed to allocate %s hash table\n", tablename);

	if (_hash_shift)
		*_hash_shift = log2qty;
	if (_hash_mask)
		*_hash_mask = (1 << log2qty) - 1;

	return table;
}
