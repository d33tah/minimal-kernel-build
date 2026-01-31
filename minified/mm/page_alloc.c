
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/interrupt.h>
#include <linux/memblock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/nodemask.h>
#include <linux/pfn.h>

static void set_pageblock_migratetype(struct page *page, int migratetype);
/* end page-isolation.h */

struct alloc_context;

#include <linux/mm_inline.h>
/* mmu_notifier.h, linux/migrate.h removed - unused/empty */
#include <linux/sched/rt.h>
#include <linux/sched/mm.h>

/* Removed: reset_page_owner, set_page_owner, split_page_owner
 * - Dead code: page_owner tracking not needed for minimal kernel (~9 LOC) */
#include <asm/sections.h>
#include <asm/tlbflush.h>
/* asm/div64.h removed - do_div not used */
#include "internal.h"

typedef int __bitwise fpi_t;
#define FPI_TO_TAIL ((__force fpi_t)BIT(1))
#define FPI_SKIP_KASAN_POISON ((__force fpi_t)BIT(2))
nodemask_t node_states[NR_NODE_STATES] __read_mostly = {
	[N_POSSIBLE] = NODE_MASK_ALL,	       [N_ONLINE] = { { [0] = 1UL } },
	[N_NORMAL_MEMORY] = { { [0] = 1UL } }, [N_MEMORY] = { { [0] = 1UL } },
	[N_CPU] = { { [0] = 1UL } },
};

atomic_long_t _totalram_pages __read_mostly;
/* totalreserve_pages removed - never read or written */
gfp_t gfp_allowed_mask __read_mostly = GFP_BOOT_MASK;
DEFINE_STATIC_KEY_MAYBE(CONFIG_INIT_ON_ALLOC_DEFAULT_ON, init_on_alloc);

DEFINE_STATIC_KEY_MAYBE(CONFIG_INIT_ON_FREE_DEFAULT_ON, init_on_free);

/* get_pcppage_migratetype inlined - returns page->index */

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

static unsigned long arch_zone_lowest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long arch_zone_highest_possible_pfn[MAX_NR_ZONES] __initdata;
/* zone_movable_pfn removed - never assigned, always 0 */
/* movable_zone removed - never assigned, only used by dead zone_movable_pfn code */

/* MAX_NUMNODES == 1, removed unused nr_node_ids/nr_online_nodes */

int page_group_by_mobility_disabled __read_mostly;

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

static void set_pfnblock_flags_mask(struct page *page, unsigned long flags,
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

static void set_pageblock_migratetype(struct page *page, int migratetype)
{
	if (unlikely(page_group_by_mobility_disabled &&
		     migratetype < MIGRATE_PCPTYPES))
		migratetype = MIGRATE_UNMOVABLE;

	set_pfnblock_flags_mask(page, (unsigned long)migratetype,
				page_to_pfn(page), MIGRATETYPE_MASK);
}

/* bad_range removed - only used in VM_BUG_ON which are no-ops */

void free_compound_page(struct page *page)
{
	/* No-op: bump allocator style - no deallocation */
}

static void prep_compound_page(struct page *page, unsigned int order)
{
	int i;
	int nr_pages = 1 << order;
	struct page *p;

	__SetPageHead(page);
	for (i = 1; i < nr_pages; i++) {
		p = page + i;
		p->mapping = TAIL_MAPPING;
		WRITE_ONCE(p->compound_head,
			   (unsigned long)page +
				   1); /* inlined set_compound_head */
	}

	/* set_compound_page_dtor inlined */
	VM_BUG_ON_PAGE(COMPOUND_PAGE_DTOR >= NR_COMPOUND_DTORS, page);
	page[1].compound_dtor = COMPOUND_PAGE_DTOR;
	page[1].compound_order = order; /* inlined set_compound_order */
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

/* add_to_free_list_tail, move_to_free_list, del_page_from_free_list inlined */

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

/* free_pages_prepare inlined into __free_pages_ok */

/* Removed: free_pcp_prepare, free_pcppages_bulk
 * - Dead code since free_unref_page and free_the_page are no-ops (~15 lines) */

/* free_one_page removed - unused (callers use __free_one_page directly) */

static void __meminit __init_single_page(struct page *page, unsigned long pfn,
					 unsigned long zone, int nid)
{
	mm_zero_struct_page(page);
	/* set_page_links inlined */
	page->flags &= ~(ZONES_MASK << ZONES_PGSHIFT);
	page->flags |= (zone & ZONES_MASK) << ZONES_PGSHIFT;
	page->flags &= ~(NODES_MASK << NODES_PGSHIFT);
	page->flags |= (nid & NODES_MASK) << NODES_PGSHIFT;
	init_page_count(page);
	page_mapcount_reset(page);

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

	/* free_pages_prepare inlined */
	if (PageMappingFlags(page))
		page->mapping = NULL;
	page->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;

	migratetype = __get_pfnblock_flags_mask(page, pfn, MIGRATETYPE_MASK);

	spin_lock_irqsave(&zone->lock, flags);
	__free_one_page(page, pfn, zone, order, migratetype, fpi_flags);
	spin_unlock_irqrestore(&zone->lock, flags);
}

static void __free_pages_core(struct page *page, unsigned int order)
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

/* page_alloc_init_late inlined into init/main.c */

/* expand() inlined into __rmqueue_smallest */
/* check_new_pages, check_pcp_refill, check_new_pcp always return false - removed */

static inline void post_alloc_hook(struct page *page, unsigned int order)
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
		/* del_page_from_free_list inlined */
		list_del(&page->lru);
		__ClearPageBuddy(page);
		set_page_private(page, 0);
		zone->free_area[current_order].nr_free--;
		/* expand() inlined */
		{
			unsigned long size = 1 << current_order;
			int high = current_order;

			while (high > order) {
				high--;
				size >>= 1;
				add_to_free_list(&page[size], zone, high,
						 migratetype);
				set_buddy_order(&page[size], high);
			}
		}
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

/* free_unref_page, free_unref_page_list moved to internal.h as static inline */

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
		/* BOOT_PAGESET_BATCH is 1, batch > 1 check removed */
		int batch = READ_ONCE(pcp->batch);
		int alloced = 0;
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
				/* is_migrate_cma always false - check removed */
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

/* rmqueue_pcplist inlined into rmqueue */

static inline struct page *rmqueue(struct zone *preferred_zone,
				   struct zone *zone, unsigned int order,
				   gfp_t gfp_flags, unsigned int alloc_flags,
				   int migratetype)
{
	unsigned long flags;
	struct page *page;

	if (likely(order <= PAGE_ALLOC_COSTLY_ORDER)) {
		/* rmqueue_pcplist inlined */
		struct per_cpu_pages *pcp;
		struct list_head *list;
		local_lock_irqsave(&pagesets.lock, flags);
		pcp = this_cpu_ptr(zone->per_cpu_pageset);
		list = &pcp->lists[(MIGRATE_PCPTYPES * order) + migratetype];
		page = __rmqueue_pcplist(zone, order, migratetype, alloc_flags,
					 pcp, list);
		local_unlock_irqrestore(&pagesets.lock, flags);
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
	__mod_zone_freepage_state(zone, -(1 << order), page->index);
	spin_unlock_irqrestore(&zone->lock, flags);
	/* check_new_pages always returns false - loop removed */
out:
	return page;

failed:
	spin_unlock_irqrestore(&zone->lock, flags);
	return NULL;
}

static bool __zone_watermark_ok(struct zone *z, unsigned int order,
				unsigned long mark, int highest_zoneidx,
				unsigned int alloc_flags, long free_pages)
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
			post_alloc_hook(page, order);
			if (order && (gfp_mask & __GFP_COMP))
				prep_compound_page(page, order);
			/* set_page_pfmemalloc/clear_page_pfmemalloc inlined */
			if (alloc_flags & ALLOC_NO_WATERMARKS)
				page->lru.next = (void *)BIT(1);
			else
				page->lru.next = NULL;
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
	/* Inlined prepare_alloc_pages */
	ac.highest_zoneidx = gfp_zone(gfp);
	ac.zonelist = node_zonelist(preferred_nid, gfp);
	ac.nodemask = nodemask;
	/* gfp_migratetype inlined */
	{
		VM_WARN_ON((gfp & (__GFP_RECLAIMABLE | __GFP_MOVABLE)) ==
			   (__GFP_RECLAIMABLE | __GFP_MOVABLE));
		if (unlikely(page_group_by_mobility_disabled))
			ac.migratetype = MIGRATE_UNMOVABLE;
		else
			ac.migratetype =
				(__force unsigned long)(gfp &
							(__GFP_RECLAIMABLE |
							 __GFP_MOVABLE)) >>
				3;
	}
	/* ac.spread_dirty_pages removed - write-only, never read */
	ac.preferred_zoneref = first_zones_zonelist(
		ac.zonelist, ac.highest_zoneidx, ac.nodemask);

	/* Inlined alloc_flags_nofragment */
	alloc_flags |= (__force int)(gfp & __GFP_KSWAPD_RECLAIM);

	page = get_page_from_freelist(alloc_gfp, order, alloc_flags, &ac);
	if (likely(page))
		goto out;

	alloc_gfp = gfp;
	/* ac.spread_dirty_pages removed - write-only */

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

/* __free_pages, free_pages moved to gfp.h as static inline */

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
		/* free_page loop removed - empty stub (bump allocator) */
		(void)alloc_end;
		(void)used; /* suppress warnings */
	}
	return (void *)addr;
}

/* nr_free_zone_pages inlined below - only called once */

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

#define BOOT_PAGESET_BATCH 1
static DEFINE_PER_CPU(struct per_cpu_pages, boot_pageset);
static DEFINE_PER_CPU(struct per_cpu_zonestat, boot_zonestats);
/* boot_nodestats removed - per_cpu_nodestats field removed */

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

/* build_all_zonelists_init removed - inlined into single caller (~9 LOC) */

/* Stub: minimal boot has enough RAM, disable page mobility grouping */
void __ref build_all_zonelists(pg_data_t *pgdat)
{
	if (system_state == SYSTEM_BOOTING) {
		__build_all_zonelists(NULL);
		per_cpu_pages_init(&per_cpu(boot_pageset, 0),
				   &per_cpu(boot_zonestats, 0));
	} else {
		__build_all_zonelists(pgdat);
	}
	page_group_by_mobility_disabled = 1;
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

/* zone_init_free_lists inlined into free_area_init_core */

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

/* memmap_init inlined into free_area_init */

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

	pcp->batch = BOOT_PAGESET_BATCH;
}

void __meminit setup_zone_pageset(struct zone *zone)
{
	struct per_cpu_pages *pcp;

	if (sizeof(struct per_cpu_zonestat) > 0)
		zone->per_cpu_zonestats = alloc_percpu(struct per_cpu_zonestat);

	zone->per_cpu_pageset = alloc_percpu(struct per_cpu_pages);
	/* for_each_possible_cpu simplified - single CPU */
	pcp = per_cpu_ptr(zone->per_cpu_pageset, 0);
	{
		struct per_cpu_zonestat *pzstats =
			per_cpu_ptr(zone->per_cpu_zonestats, 0);
		per_cpu_pages_init(pcp, pzstats);
	}

	/* zone_set_pageset_high_and_batch inlined */
	WRITE_ONCE(pcp->batch, 1);
}

void __init setup_per_cpu_pageset(void)
{
	/* pgdat, cpu variables removed - unused in single-CPU config */
	struct zone *zone;

	for_each_populated_zone(zone)
		setup_zone_pageset(zone);
}

/* zone_pcp_init inlined into free_area_init_core */

void __meminit init_currently_empty_zone(struct zone *zone,
					 unsigned long zone_start_pfn,
					 unsigned long size)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	int zone_idx = zone_idx(zone) + 1;

	if (zone_idx > pgdat->nr_zones)
		pgdat->nr_zones = zone_idx;

	zone->zone_start_pfn = zone_start_pfn;

	/* Inlined zone_init_free_lists */
	{
		unsigned int order, t;
		for_each_migratetype_order(order, t) {
			INIT_LIST_HEAD(&zone->free_area[order].free_list[t]);
			zone->free_area[order].nr_free = 0;
		}
	}
}

/* get_pfn_range_for_nid inlined into free_area_init_node */
/* adjust_zone_range_for_zone_movable removed - zone_movable_pfn always 0 */

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

	if (*zone_end_pfn < node_start_pfn || *zone_start_pfn > node_end_pfn)
		return 0;

	*zone_end_pfn = min(*zone_end_pfn, node_end_pfn);
	*zone_start_pfn = max(*zone_start_pfn, node_start_pfn);

	return *zone_end_pfn - *zone_start_pfn;
}

/* __absent_pages_in_range, zone_absent_pages_in_node, calculate_node_totalpages
   inlined into free_area_init */

/* usemap_size inlined into free_area_init_core */
/* setup_usemap inlined into free_area_init_core */
/* calc_memmap_size inlined - SPARSEMEM disabled */

/* free_area_init_core inlined into free_area_init */

/* alloc_node_mem_map inlined into free_area_init */

/* free_area_init_node inlined into free_area_init */
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

	/* free_area_init_node inlined - single node */
	if (node_online(0)) {
		pg_data_t *pgdat = NODE_DATA(0);
		unsigned long node_start_pfn = -1UL;
		unsigned long node_end_pfn = 0;
		unsigned long this_start_pfn, this_end_pfn;
		int j;

		WARN_ON(pgdat->nr_zones);

		/* get_pfn_range_for_nid inlined */
		for_each_mem_pfn_range(j, 0, &this_start_pfn, &this_end_pfn,
				       NULL) {
			node_start_pfn = min(node_start_pfn, this_start_pfn);
			node_end_pfn = max(node_end_pfn, this_end_pfn);
		}
		if (node_start_pfn == -1UL)
			node_start_pfn = 0;

		pgdat->node_id = 0;
		pgdat->node_start_pfn = node_start_pfn;

		/* Inlined calculate_node_totalpages */
		{
			unsigned long realtotalpages = 0, totalpages = 0;
			enum zone_type zi;

			for (zi = 0; zi < MAX_NR_ZONES; zi++) {
				struct zone *zone = pgdat->node_zones + zi;
				unsigned long z_start_pfn, z_end_pfn;
				unsigned long spanned, absent;
				unsigned long zsize, real_size;

				spanned = zone_spanned_pages_in_node(
					pgdat->node_id, zi, node_start_pfn,
					node_end_pfn, &z_start_pfn, &z_end_pfn);
				/* zone_absent_pages_in_node inlined */
				{
					unsigned long z_low =
						arch_zone_lowest_possible_pfn[zi];
					unsigned long z_high =
						arch_zone_highest_possible_pfn
							[zi];
					unsigned long zs_pfn, ze_pfn;
					int j;
					absent = 0;
					if (node_start_pfn || node_end_pfn) {
						zs_pfn = clamp(node_start_pfn,
							       z_low, z_high);
						ze_pfn = clamp(node_end_pfn,
							       z_low, z_high);
						/* adjust_zone_range_for_zone_movable call removed - always no-op */
						/* __absent_pages_in_range inlined */
						absent = ze_pfn - zs_pfn;
						for_each_mem_pfn_range(
							j, pgdat->node_id,
							&z_low, &z_high, NULL) {
							z_low = clamp(z_low,
								      zs_pfn,
								      ze_pfn);
							z_high = clamp(z_high,
								       zs_pfn,
								       ze_pfn);
							absent -=
								z_high - z_low;
						}
					}
				}

				zsize = spanned;
				real_size = zsize - absent;

				if (zsize)
					zone->zone_start_pfn = z_start_pfn;
				else
					zone->zone_start_pfn = 0;
				zone->spanned_pages = zsize;
				zone->present_pages = real_size;

				totalpages += zsize;
				realtotalpages += real_size;
			}

			pgdat->node_spanned_pages = totalpages;
			pgdat->node_present_pages = realtotalpages;
		}

		/* Inlined alloc_node_mem_map */
		if (pgdat->node_spanned_pages) {
			unsigned long map_start = pgdat->node_start_pfn &
						  ~(MAX_ORDER_NR_PAGES - 1);
			unsigned long map_offset =
				pgdat->node_start_pfn - map_start;

			if (!pgdat->node_mem_map) {
				unsigned long map_size, map_end;
				struct page *map;

				map_end = pgdat_end_pfn(pgdat);
				map_end = ALIGN(map_end, MAX_ORDER_NR_PAGES);
				map_size = (map_end - map_start) *
					   sizeof(struct page);
				map = memmap_alloc(map_size, SMP_CACHE_BYTES,
						   MEMBLOCK_LOW_LIMIT,
						   pgdat->node_id, false);
				if (!map)
					panic("Failed to allocate %ld bytes for node %d memory map\n",
					      map_size, pgdat->node_id);
				pgdat->node_mem_map = map + map_offset;
			}

			if (pgdat == NODE_DATA(0)) {
				mem_map = NODE_DATA(0)->node_mem_map;
				if (page_to_pfn(mem_map) !=
				    pgdat->node_start_pfn)
					mem_map -= map_offset;
			}
		}
		/* Inlined free_area_init_core */
		{
			enum zone_type fj;
			int nid = pgdat->node_id;

			lruvec_init(&pgdat->__lruvec);

			for (fj = 0; fj < MAX_NR_ZONES; fj++) {
				struct zone *zone = pgdat->node_zones + fj;
				unsigned long fsize, freesize, memmap_pages;

				fsize = zone->spanned_pages;
				freesize = zone->present_pages;

				memmap_pages =
					PAGE_ALIGN(fsize *
						   sizeof(struct page)) >>
					PAGE_SHIFT;
				if (freesize >= memmap_pages)
					freesize -= memmap_pages;

				atomic_long_set(&zone->managed_pages, freesize);
				zone->name = zone_names[fj];
				zone->zone_pgdat = NODE_DATA(nid);
				spin_lock_init(&zone->lock);
				zone->per_cpu_pageset = &boot_pageset;
				zone->per_cpu_zonestats = &boot_zonestats;

				if (!fsize)
					continue;

				{
					unsigned long usemapsize;
					unsigned long uzonesize =
						zone->spanned_pages;

					uzonesize += zone->zone_start_pfn &
						     (pageblock_nr_pages - 1);
					usemapsize = roundup(
						uzonesize, pageblock_nr_pages);
					usemapsize = usemapsize >>
						     pageblock_order;
					usemapsize *= NR_PAGEBLOCK_BITS;
					usemapsize = roundup(
						usemapsize,
						8 * sizeof(unsigned long));
					usemapsize /= 8;
					zone->pageblock_flags = NULL;
					if (usemapsize) {
						/* memblock_alloc_node inlined */
						zone->pageblock_flags =
							memblock_alloc_try_nid(
								usemapsize,
								SMP_CACHE_BYTES,
								MEMBLOCK_LOW_LIMIT,
								MEMBLOCK_ALLOC_ACCESSIBLE,
								zone_to_nid(
									zone));
						if (!zone->pageblock_flags)
							panic("Failed to allocate %ld bytes for zone %s pageblock flags on node %d\n",
							      usemapsize,
							      zone->name,
							      zone_to_nid(
								      zone));
					}
				}
				init_currently_empty_zone(
					zone, zone->zone_start_pfn, fsize);
			}
		}
	}

	/* Inlined memmap_init */
	{
		unsigned long m_start_pfn, m_end_pfn;
		unsigned long hole_pfn = 0;
		int mi, mj, zone_id = 0, nid;

		for_each_mem_pfn_range(mi, MAX_NUMNODES, &m_start_pfn,
				       &m_end_pfn, &nid) {
			struct pglist_data *node = NODE_DATA(nid);
			for (mj = 0; mj < MAX_NR_ZONES; mj++) {
				struct zone *zone = node->node_zones + mj;
				if (!populated_zone(zone))
					continue;
				memmap_init_zone_range(zone, m_start_pfn,
						       m_end_pfn, &hole_pfn);
				zone_id = mj;
			}
		}
		init_unavailable_range(hole_pfn, m_end_pfn, zone_id, nid);
	}
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
		/* free_reserved_page inlined - single caller */
		struct page *page = virt_to_page(pos);
		ClearPageReserved(page);
		init_page_count(page);
		adjust_managed_page_count(page, 1);
	}

	return pages;
}

/* page_alloc_cpu_dead, page_alloc_cpu_online, page_alloc_init moved to gfp.h */
/* calculate_totalreserve_pages, setup_per_zone_lowmem_reserve, __setup_per_zone_wmarks,
   setup_per_zone_wmarks, calculate_min_free_kbytes, init_per_zone_wmark_min removed - unused */
/* ADAPT_SCALE_* removed - unused 64-bit only code */

void *__init alloc_large_system_hash(const char *tablename,
				     unsigned long bucketsize,
				     unsigned long numentries, int scale,
				     int flags, unsigned int *_hash_shift,
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
