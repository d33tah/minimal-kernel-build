
#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/jiffies.h>
#include <linux/memblock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/pagevec.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/oom.h>
#include <linux/topology.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/nodemask.h>
#include <linux/vmalloc.h>
#include <linux/vmstat.h>
#include <linux/memremap.h>
#include <linux/stop_machine.h>
#include <linux/random.h>
#include <linux/sort.h>
#include <linux/pfn.h>
#include <linux/backing-dev.h>


struct alloc_context;

#include <linux/mm_inline.h>
#include <linux/hugetlb.h>
#include <linux/sched/mm.h>

#include <linux/kthread.h>
#include <linux/memcontrol.h>
#include <linux/lockdep.h>
#include <linux/nmi.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/div64.h>
#include "internal.h"

typedef int __bitwise fpi_t;

#define FPI_NONE		((__force fpi_t)0)

#define FPI_TO_TAIL		((__force fpi_t)BIT(1))

#define FPI_SKIP_KASAN_POISON	((__force fpi_t)BIT(2))

nodemask_t node_states[NR_NODE_STATES] __read_mostly = {
	[N_POSSIBLE] = NODE_MASK_ALL,
	[N_ONLINE] = { { [0] = 1UL } },
	[N_NORMAL_MEMORY] = { { [0] = 1UL } },
	[N_MEMORY] = { { [0] = 1UL } },
	[N_CPU] = { { [0] = 1UL } },
};

atomic_long_t _totalram_pages __read_mostly;
gfp_t gfp_allowed_mask __read_mostly = GFP_BOOT_MASK;


static inline int get_pcppage_migratetype(struct page *page)
{
	return page->index;
}

static inline void set_pcppage_migratetype(struct page *page, int migratetype)
{
	page->index = migratetype;
}

static void __free_pages_ok(struct page *page, unsigned int order,
			    fpi_t fpi_flags);

static char * const zone_names[MAX_NR_ZONES] = {
	 "Normal",
	 "Movable",
};

compound_page_dtor * const compound_page_dtors[NR_COMPOUND_DTORS] = {
	[NULL_COMPOUND_DTOR] = NULL,
	[COMPOUND_PAGE_DTOR] = free_compound_page,
};



static unsigned long arch_zone_lowest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long arch_zone_highest_possible_pfn[MAX_NR_ZONES] __initdata;

#if MAX_NUMNODES > 1
unsigned int nr_node_ids __read_mostly = MAX_NUMNODES;
#endif

int page_group_by_mobility_disabled __read_mostly;

static inline unsigned long *get_pageblock_bitmap(const struct page *page,
							unsigned long pfn)
{
	return page_zone(page)->pageblock_flags;
}

static inline int pfn_to_bitidx(const struct page *page, unsigned long pfn)
{
	pfn = pfn - round_down(page_zone(page)->zone_start_pfn, pageblock_nr_pages);
	return (pfn >> pageblock_order) * NR_PAGEBLOCK_BITS;
}

static __always_inline
unsigned long __get_pfnblock_flags_mask(const struct page *page,
					unsigned long pfn,
					unsigned long mask)
{
	unsigned long *bitmap;
	unsigned long bitidx, word_bitidx;
	unsigned long word;

	bitmap = get_pageblock_bitmap(page, pfn);
	bitidx = pfn_to_bitidx(page, pfn);
	word_bitidx = bitidx / BITS_PER_LONG;
	bitidx &= (BITS_PER_LONG-1);
	
	word = READ_ONCE(bitmap[word_bitidx]);
	return (word >> bitidx) & mask;
}

static __always_inline int get_pfnblock_migratetype(const struct page *page,
					unsigned long pfn)
{
	return __get_pfnblock_flags_mask(page, pfn, MIGRATETYPE_MASK);
}

void set_pfnblock_flags_mask(struct page *page, unsigned long flags,
					unsigned long pfn,
					unsigned long mask)
{
	unsigned long *bitmap;
	unsigned long bitidx, word_bitidx;
	unsigned long old_word, word;

	BUILD_BUG_ON(NR_PAGEBLOCK_BITS != 4);
	BUILD_BUG_ON(MIGRATE_TYPES > (1 << PB_migratetype_bits));

	bitmap = get_pageblock_bitmap(page, pfn);
	bitidx = pfn_to_bitidx(page, pfn);
	word_bitidx = bitidx / BITS_PER_LONG;
	bitidx &= (BITS_PER_LONG-1);

	VM_BUG_ON_PAGE(!zone_spans_pfn(page_zone(page), pfn), page);

	mask <<= bitidx;
	flags <<= bitidx;

	word = READ_ONCE(bitmap[word_bitidx]);
	for (;;) {
		old_word = cmpxchg(&bitmap[word_bitidx], word, (word & ~mask) | flags);
		if (word == old_word)
			break;
		word = old_word;
	}
}

/* set_pageblock_migratetype() folded into its sole caller memmap_init_range(). */

static inline unsigned int order_to_pindex(int migratetype, int order)
{
	int base = order;

	VM_BUG_ON(order > PAGE_ALLOC_COSTLY_ORDER);

	return (MIGRATE_PCPTYPES * base) + migratetype;
}

static inline bool pcp_allowed_order(unsigned int order)
{
	if (order <= PAGE_ALLOC_COSTLY_ORDER)
		return true;
	return false;
}

static inline void free_the_page(struct page *page, unsigned int order)
{
	if (pcp_allowed_order(order))		
		free_unref_page(page, order);
	else
		__free_pages_ok(page, order, FPI_NONE);
}

void free_compound_page(struct page *page)
{
	free_the_page(page, compound_order(page));
}

static void prep_compound_head(struct page *page, unsigned int order)
{
	set_compound_page_dtor(page, COMPOUND_PAGE_DTOR);
	set_compound_order(page, order);
	atomic_set(compound_mapcount_ptr(page), -1);
	atomic_set(compound_pincount_ptr(page), 0);
}

static void prep_compound_tail(struct page *head, int tail_idx)
{
	struct page *p = head + tail_idx;

	p->mapping = TAIL_MAPPING;
	set_compound_head(p, head);
}

/* prep_compound_page() folded into its sole caller prep_new_page() below. */

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

static inline void add_to_free_list_tail(struct page *page, struct zone *zone,
					 unsigned int order, int migratetype)
{
	struct free_area *area = &zone->free_area[order];

	list_add_tail(&page->lru, &area->free_list[migratetype]);
	area->nr_free++;
}

static inline void move_to_free_list(struct page *page, struct zone *zone,
				     unsigned int order, int migratetype)
{
	struct free_area *area = &zone->free_area[order];

	list_move_tail(&page->lru, &area->free_list[migratetype]);
}

static inline void del_page_from_free_list(struct page *page, struct zone *zone,
					   unsigned int order)
{
	list_del(&page->lru);
	__ClearPageBuddy(page);
	set_page_private(page, 0);
	zone->free_area[order].nr_free--;
}

static inline void __free_one_page(struct page *page,
		unsigned long pfn,
		struct zone *zone, unsigned int order,
		int migratetype, fpi_t fpi_flags)
{
	/* Simplified buddy allocator: just add to free list without merging */
	__mod_zone_freepage_state(zone, 1 << order, migratetype);

	set_buddy_order(page, order);

	if (fpi_flags & FPI_TO_TAIL)
		add_to_free_list_tail(page, zone, order, migratetype);
	else
		add_to_free_list(page, zone, order, migratetype);
}


static __always_inline bool free_pages_prepare(struct page *page,
			unsigned int order)
{
	/* Stub: minimal page freeing for simple system */
	if (PageMappingFlags(page))
		page->mapping = NULL;

	page->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;

	return true;
}

static void __meminit __init_single_page(struct page *page, unsigned long pfn,
				unsigned long zone, int nid)
{
	mm_zero_struct_page(page);
	set_page_links(page, zone, nid, pfn);
	init_page_count(page);
	page_mapcount_reset(page);

	INIT_LIST_HEAD(&page->lru);
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

	if (!free_pages_prepare(page, order))
		return;

	migratetype = get_pfnblock_migratetype(page, pfn);

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

	/* Stub: skip memory shuffling for minimal system */
}

static void prep_new_page(struct page *page, unsigned int order, gfp_t gfp_flags,
							unsigned int alloc_flags)
{
	/* folded sole caller of post_alloc_hook(): minimal post-alloc setup */
	set_page_private(page, 0);
	set_page_refcounted(page);

	if (order && (gfp_flags & __GFP_COMP)) {
		int i, nr_pages = 1 << order;

		__SetPageHead(page);
		for (i = 1; i < nr_pages; i++)
			prep_compound_tail(page, i);
		prep_compound_head(page, order);
	}


	if (alloc_flags & ALLOC_NO_WATERMARKS)
		set_page_pfmemalloc(page);
	else
		clear_page_pfmemalloc(page);
}

static __always_inline
struct page *__rmqueue_smallest(struct zone *zone, unsigned int order,
						int migratetype)
{
	unsigned int current_order;
	struct free_area *area;
	struct page *page;


	for (current_order = order; current_order < MAX_ORDER; ++current_order) {
		int high = current_order;
		unsigned long size;

		area = &(zone->free_area[current_order]);
		page = get_page_from_free_area(area, migratetype);
		if (!page)
			continue;
		del_page_from_free_list(page, zone, current_order);
		/* folded sole caller of expand(): split the buddy block down to @order */
		size = 1 << high;
		while (high > (int)order) {
			high--;
			size >>= 1;
			add_to_free_list(&page[size], zone, high, migratetype);
			set_buddy_order(&page[size], high);
		}
		set_pcppage_migratetype(page, migratetype);
		return page;
	}

	return NULL;
}

static int fallbacks[MIGRATE_TYPES][3] = {
	[MIGRATE_UNMOVABLE]   = { MIGRATE_RECLAIMABLE, MIGRATE_MOVABLE,   MIGRATE_TYPES },
	[MIGRATE_MOVABLE]     = { MIGRATE_RECLAIMABLE, MIGRATE_UNMOVABLE, MIGRATE_TYPES },
	[MIGRATE_RECLAIMABLE] = { MIGRATE_UNMOVABLE,   MIGRATE_MOVABLE,   MIGRATE_TYPES },
};

static void steal_suitable_fallback(struct zone *zone, struct page *page,
		unsigned int alloc_flags, int start_type)
{
	/* Minimal stub: just move page to target type */
	unsigned int current_order = buddy_order(page);
	move_to_free_list(page, zone, current_order, start_type);
}

static int find_suitable_fallback(struct free_area *area, unsigned int order,
			int migratetype)
{
	int i;
	int fallback_mt;

	if (area->nr_free == 0)
		return -1;

	for (i = 0;; i++) {
		fallback_mt = fallbacks[migratetype][i];
		if (fallback_mt == MIGRATE_TYPES)
			break;

		if (free_area_empty(area, fallback_mt))
			continue;

		return fallback_mt;
	}

	return -1;
}



static __always_inline bool
__rmqueue_fallback(struct zone *zone, int order, int start_migratetype,
						unsigned int alloc_flags)
{
	/* Simplified fallback: try all orders, take first match */
	struct free_area *area;
	int current_order;
	int fallback_mt;
	struct page *page;

	for (current_order = MAX_ORDER - 1; current_order >= order; --current_order) {
		area = &(zone->free_area[current_order]);
		fallback_mt = find_suitable_fallback(area, current_order,
				start_migratetype);
		if (fallback_mt == -1)
			continue;

		page = get_page_from_free_area(area, fallback_mt);
		steal_suitable_fallback(zone, page, alloc_flags, start_migratetype);
		return true;
	}

	return false;
}

static __always_inline struct page *
__rmqueue(struct zone *zone, unsigned int order, int migratetype,
						unsigned int alloc_flags)
{
	struct page *page;

retry:
	page = __rmqueue_smallest(zone, order, migratetype);
	if (unlikely(!page)) {
		if (__rmqueue_fallback(zone, order, migratetype, alloc_flags))
			goto retry;
	}
	return page;
}

static int rmqueue_bulk(struct zone *zone, unsigned int order,
			unsigned long count, struct list_head *list,
			int migratetype, unsigned int alloc_flags)
{
	int i, allocated = 0;

	
	spin_lock(&zone->lock);
	for (i = 0; i < count; ++i) {
		struct page *page = __rmqueue(zone, order, migratetype,
								alloc_flags);
		if (unlikely(page == NULL))
			break;

		list_add_tail(&page->lru, list);
		allocated++;
	}

	
	__mod_zone_page_state(zone, NR_FREE_PAGES, -(i << order));
	spin_unlock(&zone->lock);
	return allocated;
}


static bool free_unref_page_prepare(struct page *page, unsigned long pfn,
							unsigned int order)
{
	int migratetype;

	if (!free_pages_prepare(page, order))
		return false;

	migratetype = get_pfnblock_migratetype(page, pfn);
	set_pcppage_migratetype(page, migratetype);
	return true;
}

static void free_unref_page_commit(struct page *page, int migratetype,
				   unsigned int order)
{
	struct zone *zone = page_zone(page);
	struct per_cpu_pages *pcp;
	int pindex;

	pcp = this_cpu_ptr(zone->per_cpu_pageset);
	pindex = order_to_pindex(migratetype, order);
	list_add(&page->lru, &pcp->lists[pindex]);
}

void free_unref_page(struct page *page, unsigned int order)
{
	unsigned long flags;
	unsigned long pfn = page_to_pfn(page);
	int migratetype;

	if (!free_unref_page_prepare(page, pfn, order))
		return;

	
	migratetype = get_pcppage_migratetype(page);
	if (unlikely(migratetype >= MIGRATE_PCPTYPES))
		migratetype = MIGRATE_MOVABLE;

	local_irq_save(flags);
	free_unref_page_commit(page, migratetype, order);
	local_irq_restore(flags);
}

void free_unref_page_list(struct list_head *list)
{
	struct page *page, *next;
	unsigned long flags;
	int batch_count = 0;
	int migratetype;

	
	list_for_each_entry_safe(page, next, list, lru) {
		unsigned long pfn = page_to_pfn(page);
		if (!free_unref_page_prepare(page, pfn, 0)) {
			list_del(&page->lru);
			continue;
		}
	}

	local_irq_save(flags);
	list_for_each_entry_safe(page, next, list, lru) {
		
		migratetype = get_pcppage_migratetype(page);
		if (unlikely(migratetype >= MIGRATE_PCPTYPES))
			migratetype = MIGRATE_MOVABLE;

		free_unref_page_commit(page, migratetype, 0);

		
		if (++batch_count == SWAP_CLUSTER_MAX) {
			local_irq_restore(flags);
			batch_count = 0;
			local_irq_save(flags);
		}
	}
	local_irq_restore(flags);
}

void split_page(struct page *page, unsigned int order)
{
	int i;

	VM_BUG_ON_PAGE(PageCompound(page), page);
	VM_BUG_ON_PAGE(!page_count(page), page);

	for (i = 1; i < (1 << order); i++)
		set_page_refcounted(page + i);
}


static inline
struct page *__rmqueue_pcplist(struct zone *zone, unsigned int order,
			int migratetype,
			unsigned int alloc_flags,
			struct per_cpu_pages *pcp,
			struct list_head *list)
{
	struct page *page;

	if (list_empty(list)) {
		int batch = READ_ONCE(pcp->batch);

		if (batch > 1)
			batch = max(batch >> order, 2);
		rmqueue_bulk(zone, order,
				batch, list,
				migratetype, alloc_flags);

		if (unlikely(list_empty(list)))
			return NULL;
	}

	page = list_first_entry(list, struct page, lru);
	list_del(&page->lru);

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
	local_irq_save(flags);
	pcp = this_cpu_ptr(zone->per_cpu_pageset);
	list = &pcp->lists[order_to_pindex(migratetype, order)];
	page = __rmqueue_pcplist(zone, order, migratetype, alloc_flags, pcp, list);
	local_irq_restore(flags);
	return page;
}

static inline
struct page *rmqueue(struct zone *preferred_zone,
			struct zone *zone, unsigned int order,
			gfp_t gfp_flags, unsigned int alloc_flags,
			int migratetype)
{
	unsigned long flags;
	struct page *page;

	if (likely(pcp_allowed_order(order))) {
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

out:
	return page;

failed:
	spin_unlock_irqrestore(&zone->lock, flags);
	return NULL;
}

static inline bool zone_watermark_fast(struct zone *z, unsigned int order,
				unsigned long mark, int highest_zoneidx,
				unsigned int alloc_flags, gfp_t gfp_mask)
{
	/* Simplified fast watermark check for minimal kernel
	 * (folded from the sole-caller-only __zone_watermark_ok). */
	long free_pages = zone_page_state(z, NR_FREE_PAGES);
	long min = mark;

	/* Apply alloc_flags adjustments */
	if (alloc_flags & ALLOC_HIGH)
		min -= min / 2;
	if (alloc_flags & (ALLOC_HARDER|ALLOC_OOM))
		min -= min / 2;

	/* Basic free pages check (lowmem_reserve is always 0 in this minimal kernel) */
	return free_pages > min;
}


static inline unsigned int
alloc_flags_nofragment(struct zone *zone, gfp_t gfp_mask)
{
	unsigned int alloc_flags;

	
	alloc_flags = (__force int) (gfp_mask & __GFP_KSWAPD_RECLAIM);

	return alloc_flags;
}

static struct page *
get_page_from_freelist(gfp_t gfp_mask, unsigned int order, int alloc_flags,
						const struct alloc_context *ac)
{
	/* Minimal stub: simplified zone iteration */
	struct zoneref *z;
	struct zone *zone;
	struct page *page;

	z = ac->preferred_zoneref;
	for_next_zone_zonelist_nodemask(zone, z, ac->highest_zoneidx, ac->nodemask) {
		unsigned long mark = wmark_pages(zone, alloc_flags & ALLOC_WMARK_MASK);

		/* Skip watermark check if NO_WATERMARKS flag set */
		if (!(alloc_flags & ALLOC_NO_WATERMARKS) &&
		    !zone_watermark_fast(zone, order, mark, ac->highest_zoneidx, alloc_flags, gfp_mask))
			continue;

		page = rmqueue(ac->preferred_zoneref->zone, zone, order,
				gfp_mask, alloc_flags, ac->migratetype);
		if (page) {
			prep_new_page(page, order, gfp_mask, alloc_flags);
			return page;
		}
	}

	return NULL;
}


static inline int __gfp_pfmemalloc_flags(gfp_t gfp_mask)
{
	if (unlikely(gfp_mask & __GFP_NOMEMALLOC))
		return 0;
	if (gfp_mask & __GFP_MEMALLOC)
		return ALLOC_NO_WATERMARKS;

	return 0;
}

bool gfp_pfmemalloc_allowed(gfp_t gfp_mask)
{
	return !!__gfp_pfmemalloc_flags(gfp_mask);
}


static inline bool prepare_alloc_pages(gfp_t gfp_mask, unsigned int order,
		int preferred_nid, nodemask_t *nodemask,
		struct alloc_context *ac, gfp_t *alloc_gfp,
		unsigned int *alloc_flags)
{
	ac->highest_zoneidx = gfp_zone(gfp_mask);
	ac->zonelist = node_zonelist(preferred_nid, gfp_mask);
	ac->nodemask = nodemask;
	ac->migratetype = gfp_migratetype(gfp_mask);

	might_sleep_if(gfp_mask & __GFP_DIRECT_RECLAIM);


	ac->preferred_zoneref = first_zones_zonelist(ac->zonelist,
					ac->highest_zoneidx, ac->nodemask);

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
	struct alloc_context ac = { };

	
	if (WARN_ON_ONCE_GFP(order >= MAX_ORDER, gfp))
		return NULL;

	gfp &= gfp_allowed_mask;
	
	gfp = current_gfp_context(gfp);
	alloc_gfp = gfp;
	if (!prepare_alloc_pages(gfp, order, preferred_nid, nodemask, &ac,
			&alloc_gfp, &alloc_flags))
		return NULL;

	
	alloc_flags |= alloc_flags_nofragment(ac.preferred_zoneref->zone, gfp);

	
	page = get_page_from_freelist(alloc_gfp, order, alloc_flags, &ac);

	return page;
}

struct folio *__folio_alloc(gfp_t gfp, unsigned int order, int preferred_nid,
		nodemask_t *nodemask)
{
	struct page *page = __alloc_pages(gfp | __GFP_COMP, order,
			preferred_nid, nodemask);

	return (struct folio *)page;
}

unsigned long __get_free_pages(gfp_t gfp_mask, unsigned int order)
{
	struct page *page;

	page = alloc_pages(gfp_mask & ~__GFP_HIGHMEM, order);
	if (!page)
		return 0;
	return (unsigned long) page_address(page);
}

void __free_pages(struct page *page, unsigned int order)
{
	if (put_page_testzero(page))
		free_the_page(page, order);
	else if (!PageHead(page))
		while (order-- > 0)
			free_the_page(page + (1 << order), order);
}

void free_pages(unsigned long addr, unsigned int order)
{
	if (addr != 0) {
		VM_BUG_ON(!virt_addr_valid((void *)addr));
		__free_pages(virt_to_page((void *)addr), order);
	}
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

static void zoneref_set_zone(struct zone *zone, struct zoneref *zoneref)
{
	zoneref->zone = zone;
	zoneref->zone_idx = zone_idx(zone);
}

static int build_zonerefs_node(pg_data_t *pgdat, struct zoneref *zonerefs)
{
	struct zone *zone;
	enum zone_type zone_type = MAX_NR_ZONES;
	int nr_zones = 0;

	do {
		zone_type--;
		zone = pgdat->node_zones + zone_type;
		if (populated_zone(zone)) {
			zoneref_set_zone(zone, &zonerefs[nr_zones++]);
		}
	} while (zone_type);

	return nr_zones;
}

static void build_zonelists(pg_data_t *pgdat)
{
	struct zoneref *zonerefs;
	int nr_zones;

	/* Simplified: single-node system, just build local node zonelist */
	zonerefs = pgdat->node_zonelists[ZONELIST_FALLBACK]._zonerefs;
	nr_zones = build_zonerefs_node(pgdat, zonerefs);
	zonerefs += nr_zones;

	zonerefs->zone = NULL;
	zonerefs->zone_idx = 0;
}

static void per_cpu_pages_init(struct per_cpu_pages *pcp);

#define BOOT_PAGESET_HIGH	0
#define BOOT_PAGESET_BATCH	1
static DEFINE_PER_CPU(struct per_cpu_pages, boot_pageset);

static void __build_all_zonelists(void *data)
{
	int nid;
	int __maybe_unused cpu;
	static DEFINE_SPINLOCK(lock);

	spin_lock(&lock);

	for_each_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);

		build_zonelists(pgdat);
	}

	spin_unlock(&lock);
}

static noinline void __init
build_all_zonelists_init(void)
{
	int cpu;

	__build_all_zonelists(NULL);

	
	for_each_possible_cpu(cpu)
		per_cpu_pages_init(&per_cpu(boot_pageset, cpu));
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

static void __meminit memmap_init_range(unsigned long size, int nid, unsigned long zone,
		unsigned long start_pfn, unsigned long zone_end_pfn,
		enum meminit_context context,
		struct vmem_altmap *altmap, int migratetype)
{
	unsigned long pfn, end_pfn = start_pfn + size;
	struct page *page;

	if (highest_memmap_pfn < end_pfn - 1)
		highest_memmap_pfn = end_pfn - 1;

	for (pfn = start_pfn; pfn < end_pfn; ) {
		page = pfn_to_page(pfn);
		__init_single_page(page, pfn, zone, nid);
		if (context == MEMINIT_HOTPLUG)
			__SetPageReserved(page);

		
		if (IS_ALIGNED(pfn, pageblock_nr_pages)) {
			int mt = migratetype;

			if (unlikely(page_group_by_mobility_disabled &&
				     mt < MIGRATE_PCPTYPES))
				mt = MIGRATE_UNMOVABLE;
			set_pfnblock_flags_mask(page, (unsigned long)mt,
						page_to_pfn(page),
						MIGRATETYPE_MASK);
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
					  unsigned long epfn,
					  int zone, int node)
{
	unsigned long pfn;
	u64 pgcnt = 0;

	for (pfn = spfn; pfn < epfn; pfn++) {
		if (!pfn_valid(ALIGN_DOWN(pfn, pageblock_nr_pages))) {
			pfn = ALIGN_DOWN(pfn, pageblock_nr_pages)
				+ pageblock_nr_pages - 1;
			continue;
		}
		__init_single_page(pfn_to_page(pfn), pfn, zone, node);
		__SetPageReserved(pfn_to_page(pfn));
		pgcnt++;
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

static int zone_batchsize(struct zone *zone)
{
	/* Stub: simplified batch size for minimal kernel */
	return 1;
}

static int zone_highsize(struct zone *zone, int batch, int cpu_online)
{
	/* Stub: simplified high watermark for minimal kernel */
	return batch << 2;
}

static void pageset_update(struct per_cpu_pages *pcp, unsigned long high,
		unsigned long batch)
{
	WRITE_ONCE(pcp->batch, batch);
}

static void per_cpu_pages_init(struct per_cpu_pages *pcp)
{
	int pindex;

	memset(pcp, 0, sizeof(*pcp));

	for (pindex = 0; pindex < NR_PCP_LISTS; pindex++)
		INIT_LIST_HEAD(&pcp->lists[pindex]);


	pcp->batch = BOOT_PAGESET_BATCH;
}

static void __zone_set_pageset_high_and_batch(struct zone *zone, unsigned long high,
		unsigned long batch)
{
	struct per_cpu_pages *pcp;
	int cpu;

	for_each_possible_cpu(cpu) {
		pcp = per_cpu_ptr(zone->per_cpu_pageset, cpu);
		pageset_update(pcp, high, batch);
	}
}

static void zone_set_pageset_high_and_batch(struct zone *zone, int cpu_online)
{
	int new_high, new_batch;

	new_batch = max(1, zone_batchsize(zone));
	new_high = zone_highsize(zone, new_batch, cpu_online);

	if (zone->pageset_high == new_high &&
	    zone->pageset_batch == new_batch)
		return;

	zone->pageset_high = new_high;
	zone->pageset_batch = new_batch;

	__zone_set_pageset_high_and_batch(zone, new_high, new_batch);
}

void __meminit setup_zone_pageset(struct zone *zone)
{
	int cpu;

	zone->per_cpu_pageset = alloc_percpu(struct per_cpu_pages);
	for_each_possible_cpu(cpu) {
		struct per_cpu_pages *pcp;

		pcp = per_cpu_ptr(zone->per_cpu_pageset, cpu);
		per_cpu_pages_init(pcp);
	}

	zone_set_pageset_high_and_batch(zone, 0);
}

void __init setup_per_cpu_pageset(void)
{
	struct zone *zone;
	int __maybe_unused cpu;

	for_each_populated_zone(zone)
		setup_zone_pageset(zone);
}

static __meminit void zone_pcp_init(struct zone *zone)
{
	
	zone->per_cpu_pageset = &boot_pageset;
	zone->pageset_high = BOOT_PAGESET_HIGH;
	zone->pageset_batch = BOOT_PAGESET_BATCH;

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

	zone_init_free_lists(zone);
}

static void __init get_pfn_range_for_nid(unsigned int nid,
			unsigned long *start_pfn, unsigned long *end_pfn)
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

static unsigned long __init zone_spanned_pages_in_node(int nid,
					unsigned long zone_type,
					unsigned long node_start_pfn,
					unsigned long node_end_pfn,
					unsigned long *zone_start_pfn,
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

static unsigned long __init __absent_pages_in_range(int nid,
				unsigned long range_start_pfn,
				unsigned long range_end_pfn)
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


static unsigned long __init zone_absent_pages_in_node(int nid,
					unsigned long zone_type,
					unsigned long node_start_pfn,
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

	nr_absent = __absent_pages_in_range(nid, zone_start_pfn, zone_end_pfn);


	return nr_absent;
}

static void __init calculate_node_totalpages(struct pglist_data *pgdat,
						unsigned long node_start_pfn,
						unsigned long node_end_pfn)
{
	unsigned long totalpages = 0;
	enum zone_type i;

	for (i = 0; i < MAX_NR_ZONES; i++) {
		struct zone *zone = pgdat->node_zones + i;
		unsigned long zone_start_pfn, zone_end_pfn;
		unsigned long spanned, absent;
		unsigned long size, real_size;

		spanned = zone_spanned_pages_in_node(pgdat->node_id, i,
						     node_start_pfn,
						     node_end_pfn,
						     &zone_start_pfn,
						     &zone_end_pfn);
		absent = zone_absent_pages_in_node(pgdat->node_id, i,
						   node_start_pfn,
						   node_end_pfn);

		size = spanned;
		real_size = size - absent;

		if (size)
			zone->zone_start_pfn = zone_start_pfn;
		else
			zone->zone_start_pfn = 0;
		zone->spanned_pages = size;
		zone->present_pages = real_size;

		totalpages += size;
	}

	pgdat->node_spanned_pages = totalpages;
}

static unsigned long __init usemap_size(unsigned long zone_start_pfn, unsigned long zonesize)
{
	unsigned long usemapsize;

	zonesize += zone_start_pfn & (pageblock_nr_pages-1);
	usemapsize = roundup(zonesize, pageblock_nr_pages);
	usemapsize = usemapsize >> pageblock_order;
	usemapsize *= NR_PAGEBLOCK_BITS;
	usemapsize = roundup(usemapsize, 8 * sizeof(unsigned long));

	return usemapsize / 8;
}

static void __ref setup_usemap(struct zone *zone)
{
	unsigned long usemapsize = usemap_size(zone->zone_start_pfn,
					       zone->spanned_pages);
	zone->pageblock_flags = NULL;
	if (usemapsize) {
		zone->pageblock_flags =
			memblock_alloc_node(usemapsize, SMP_CACHE_BYTES,
					    zone_to_nid(zone));
		if (!zone->pageblock_flags)
			panic("Failed to allocate %ld bytes for zone %s pageblock flags on node %d\n",
			      usemapsize, zone->name, zone_to_nid(zone));
	}
}

static unsigned long __init calc_memmap_size(unsigned long spanned_pages,
						unsigned long present_pages)
{
	return PAGE_ALIGN(spanned_pages * sizeof(struct page)) >> PAGE_SHIFT;
}

static void __meminit pgdat_init_internals(struct pglist_data *pgdat)
{
	lruvec_init(&pgdat->__lruvec);
}

static void __meminit zone_init_internals(struct zone *zone, enum zone_type idx, int nid,
							unsigned long remaining_pages)
{
	atomic_long_set(&zone->managed_pages, remaining_pages);
	zone->name = zone_names[idx];
	zone->zone_pgdat = NODE_DATA(nid);
	spin_lock_init(&zone->lock);
	zone_pcp_init(zone);
}

static void __init free_area_init_core(struct pglist_data *pgdat)
{
	enum zone_type j;
	int nid = pgdat->node_id;

	pgdat_init_internals(pgdat);

	for (j = 0; j < MAX_NR_ZONES; j++) {
		struct zone *zone = pgdat->node_zones + j;
		unsigned long size, freesize, memmap_pages;

		size = zone->spanned_pages;
		freesize = zone->present_pages;

		
		memmap_pages = calc_memmap_size(size, freesize);
		if (freesize >= memmap_pages)
			freesize -= memmap_pages;

		/* dma_reserve, nr_kernel_pages, nr_all_pages removed - never read */

		zone_init_internals(zone, j, nid, freesize);

		if (!size)
			continue;

		setup_usemap(zone);
		init_currently_empty_zone(zone, zone->zone_start_pfn, size);
	}
}

static void __init alloc_node_mem_map(struct pglist_data *pgdat)
{
	unsigned long __maybe_unused start = 0;
	unsigned long __maybe_unused offset = 0;

	
	if (!pgdat->node_spanned_pages)
		return;

	start = pgdat->node_start_pfn & ~(MAX_ORDER_NR_PAGES - 1);
	offset = pgdat->node_start_pfn - start;
	
	if (!pgdat->node_mem_map) {
		unsigned long size, end;
		struct page *map;

		
		end = pgdat_end_pfn(pgdat);
		end = ALIGN(end, MAX_ORDER_NR_PAGES);
		size =  (end - start) * sizeof(struct page);
		map = memblock_alloc_try_nid_raw(size, SMP_CACHE_BYTES,
						 MEMBLOCK_LOW_LIMIT,
						 MEMBLOCK_ALLOC_ACCESSIBLE,
						 pgdat->node_id);
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

	
	WARN_ON(pgdat->nr_zones);

	get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);

	pgdat->node_id = nid;
	pgdat->node_start_pfn = start_pfn;

	calculate_node_totalpages(pgdat, start_pfn, end_pfn);

	alloc_node_mem_map(pgdat);

	free_area_init_core(pgdat);
}


static unsigned long __init find_min_pfn_with_active_regions(void)
{
	return PHYS_PFN(memblock_start_of_DRAM());
}

void __init free_area_init(unsigned long *max_zone_pfn)
{
	unsigned long start_pfn, end_pfn;
	int i;

	/* Minimal zone setup - just set up basic pfn ranges */
	memset(arch_zone_lowest_possible_pfn, 0,
				sizeof(arch_zone_lowest_possible_pfn));
	memset(arch_zone_highest_possible_pfn, 0,
				sizeof(arch_zone_highest_possible_pfn));

	start_pfn = find_min_pfn_with_active_regions();

	/* Simple linear zone setup without movable zones */
	for (i = 0; i < MAX_NR_ZONES; i++) {
		if (i == ZONE_MOVABLE)
			continue;
		end_pfn = max(max_zone_pfn[i], start_pfn);
		arch_zone_lowest_possible_pfn[i] = start_pfn;
		arch_zone_highest_possible_pfn[i] = end_pfn;
		start_pfn = end_pfn;
	}

	/* Basic node initialization (single node, MAX_NUMNODES=1) */
	free_area_init_node(0);

	memmap_init();
}


unsigned long free_reserved_area(void *start, void *end, int poison, const char *s)
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

void __init mem_init_print_info(void)
{

}


void __init page_alloc_init(void)
{
	/*
	 * Single-CPU build with no CPU hotplug: the CPUHP_PAGE_ALLOC
	 * dead/online callbacks would only ever run on a hotplug event,
	 * which never happens here, so nothing needs to be registered.
	 */
}

static void __setup_per_zone_wmarks(void)
{
	/* Minimal stub: set basic watermarks without complex calculations */
	struct zone *zone;
	unsigned long flags;

	for_each_zone(zone) {
		spin_lock_irqsave(&zone->lock, flags);
		zone->_watermark[WMARK_MIN] = 128;
		zone->_watermark[WMARK_LOW] = 256;
		zone->_watermark[WMARK_HIGH] = 512;
		spin_unlock_irqrestore(&zone->lock, flags);
	}
}

static void setup_per_zone_wmarks(void)
{
	static DEFINE_SPINLOCK(lock);

	spin_lock(&lock);
	__setup_per_zone_wmarks();
	spin_unlock(&lock);
	/* zone_pcp_update removed - not needed for single CPU minimal kernel */
}

int __meminit init_per_zone_wmark_min(void)
{
	setup_per_zone_wmarks();

	return 0;
}
postcore_initcall(init_per_zone_wmark_min)

void *__init alloc_large_system_hash(const char *tablename,
				     unsigned long bucketsize,
				     unsigned long numentries,
				     int scale,
				     int flags,
				     unsigned int *_hash_shift,
				     unsigned int *_hash_mask,
				     unsigned long low_limit,
				     unsigned long high_limit)
{
	unsigned long log2qty, size;
	void *table;

	/* Minimal stub: simple hash table allocation */
	if (!numentries)
		numentries = 256; /* Small default */
	numentries = roundup_pow_of_two(numentries);
	log2qty = ilog2(numentries);
	size = bucketsize << log2qty;

	/*
	 * The only runtime caller (dcache_init_early) always passes HASH_EARLY
	 * because hashdist is the compile-time constant 0, so the non-early
	 * alloc_pages_exact() arm is unreachable. Always use memblock.
	 */
	table = memblock_alloc(size, SMP_CACHE_BYTES);

	if (!table)
		panic("Failed to allocate %s hash table\n", tablename);

	if (_hash_shift)
		*_hash_shift = log2qty;
	if (_hash_mask)
		*_hash_mask = (1 << log2qty) - 1;

	return table;
}

