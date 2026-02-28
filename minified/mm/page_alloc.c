#include <linux/memblock.h>
struct vmem_altmap;

/* end page-isolation.h */

struct alloc_context;

#include <linux/sched/mm.h>

#include "internal.h"

atomic_long_t _totalram_pages __read_mostly;
gfp_t gfp_allowed_mask __read_mostly = GFP_BOOT_MASK;

static void __free_pages_ok(struct page *page, unsigned int order);

static char *const zone_names[MAX_NR_ZONES] = {
	"Normal",
	"Movable",
};

static void free_compound_page(struct page *page);
compound_page_dtor *const compound_page_dtors[NR_COMPOUND_DTORS] = {
	[NULL_COMPOUND_DTOR] = NULL,
	[COMPOUND_PAGE_DTOR] = free_compound_page,
};

static unsigned long arch_zone_lowest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long arch_zone_highest_possible_pfn[MAX_NR_ZONES] __initdata;

/* page_group_by_mobility_disabled always 1 - hardcoded */

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

static void set_pfnblock_flags_mask(struct page *page, unsigned long flags,
				    unsigned long pfn, unsigned long mask)
{
	unsigned long *bitmap;
	unsigned long bitidx, word_bitidx;
	unsigned long old_word, word;

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

static void free_compound_page(struct page *page)
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

	page[1].compound_dtor = COMPOUND_PAGE_DTOR;
	page[1].compound_order = order; /* inlined set_compound_order */
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

static inline void __free_one_page(struct page *page, unsigned long pfn,
				   struct zone *zone, unsigned int order,
				   int migratetype)
{
	struct free_area *area = &zone->free_area[order];
	__mod_zone_freepage_state(zone, 1 << order, migratetype);
	set_buddy_order(page, order);
	list_add_tail(&page->lru, &area->free_list[migratetype]);
	area->nr_free++;
}

static void __meminit __init_single_page(struct page *page, unsigned long pfn,
					 unsigned long zone, int nid)
{
	mm_zero_struct_page(page);
	page->flags &= ~(ZONES_MASK << ZONES_PGSHIFT);
	page->flags |= (zone & ZONES_MASK) << ZONES_PGSHIFT;
	page->flags &= ~(NODES_MASK << NODES_PGSHIFT);
	page->flags |= (nid & NODES_MASK) << NODES_PGSHIFT;
	set_page_count(page, 1); /* init_page_count inlined */
	atomic_set(&page->_mapcount, -1); /* page_mapcount_reset inlined */

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

static void __free_pages_ok(struct page *page, unsigned int order)
{
	unsigned long flags;
	int migratetype;
	unsigned long pfn = page_to_pfn(page);
	struct zone *zone = page_zone(page);

	if (PageMappingFlags(page))
		page->mapping = NULL;
	page->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;

	migratetype = __get_pfnblock_flags_mask(page, pfn, MIGRATETYPE_MASK);

	spin_lock_irqsave(&zone->lock, flags);
	__free_one_page(page, pfn, zone, order, migratetype);
	spin_unlock_irqrestore(&zone->lock, flags);
}

void __init memblock_free_pages(struct page *page, unsigned long pfn,
				unsigned int order)
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

	__free_pages_ok(page, order);
}

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
		list_del(&page->lru);
		__ClearPageBuddy(page);
		set_page_private(page, 0);
		zone->free_area[current_order].nr_free--;
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

static inline struct page *
__rmqueue_pcplist(struct zone *zone, unsigned int order, int migratetype,
		  unsigned int alloc_flags, struct per_cpu_pages *pcp,
		  struct list_head *list)
{
	struct page *page;

	if (list_empty(list)) {
		int batch = READ_ONCE(pcp->batch);
		int alloced = 0;
		{
			int i;
			spin_lock(&zone->lock);
			for (i = 0; i < batch; ++i) {
				struct page *pg = __rmqueue_smallest(
					zone, order, migratetype);
				if (unlikely(pg == NULL))
					break;
				list_add_tail(&pg->lru, list);
				alloced++;
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
	return page;
}

static inline struct page *rmqueue(struct zone *zone, unsigned int order,
				   gfp_t gfp_flags, unsigned int alloc_flags,
				   int migratetype)
{
	unsigned long flags;
	struct page *page;

	if (likely(order <= PAGE_ALLOC_COSTLY_ORDER)) {
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

	page = NULL;
	spin_lock_irqsave(&zone->lock, flags);

	page = __rmqueue_smallest(zone, order, migratetype);
	if (!page)
		goto failed;
	__mod_zone_freepage_state(zone, -(1 << order), page->index);
	spin_unlock_irqrestore(&zone->lock, flags);
out:
	return page;

failed:
	spin_unlock_irqrestore(&zone->lock, flags);
	return NULL;
}

static struct page *get_page_from_freelist(gfp_t gfp_mask, unsigned int order,
					   int alloc_flags,
					   const struct alloc_context *ac)
{
	struct zoneref *z;
	struct zone *zone;
	struct page *page;

	z = ac->preferred_zoneref;
	for_next_zone_zonelist_nodemask(zone, z, ac->highest_zoneidx,
					ac->nodemask)
	{
		unsigned long mark =
			wmark_pages(zone, alloc_flags & ALLOC_WMARK_MASK);

		if (!(alloc_flags & ALLOC_NO_WATERMARKS)) {
			long free_pages = zone_page_state(zone, NR_FREE_PAGES);
			if (free_pages <= (long)mark)
				continue;
		}

		page = rmqueue(zone, order, gfp_mask, alloc_flags,
			       ac->migratetype);
		if (page) {
			set_page_private(page, 0);
			set_page_refcounted(page);
			if (order && (gfp_mask & __GFP_COMP))
				prep_compound_page(page, order);
			page->lru.next = NULL;
			return page;
		}
	}

	return NULL;
}

static inline struct page *__alloc_pages_slowpath(gfp_t gfp_mask,
						  unsigned int order,
						  struct alloc_context *ac)
{
	/* Retry with no watermarks - for hello-world, if fast path fails just
	 * skip watermarks rather than complex OOM/reclaim logic */
	ac->preferred_zoneref = first_zones_zonelist(
		ac->zonelist, ac->highest_zoneidx, ac->nodemask);
	if (!ac->preferred_zoneref->zone)
		return NULL;
	return get_page_from_freelist(gfp_mask, order, ALLOC_NO_WATERMARKS, ac);
}

struct page *__alloc_pages(gfp_t gfp, unsigned int order, int preferred_nid,
			   nodemask_t *nodemask)
{
	struct page *page;
	unsigned int alloc_flags = ALLOC_WMARK_LOW;
	gfp_t alloc_gfp;
	struct alloc_context ac = {};

	if (order >= MAX_ORDER)
		return NULL;

	gfp &= gfp_allowed_mask;

	gfp = current_gfp_context(gfp);
	alloc_gfp = gfp;
	ac.highest_zoneidx = gfp_zone(gfp);
	ac.zonelist = node_zonelist(preferred_nid, gfp);
	ac.nodemask = nodemask;
	ac.migratetype = MIGRATE_UNMOVABLE;
	ac.preferred_zoneref = first_zones_zonelist(
		ac.zonelist, ac.highest_zoneidx, ac.nodemask);

	page = get_page_from_freelist(alloc_gfp, order, alloc_flags, &ac);
	if (likely(page))
		return page;

	return __alloc_pages_slowpath(alloc_gfp, order, &ac);
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

static void build_zonelists(pg_data_t *pgdat)
{
	struct zoneref *zonerefs;
	struct zone *zone;
	enum zone_type zone_type = MAX_NR_ZONES;
	int nr_zones = 0;

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

void __ref build_all_zonelists(pg_data_t *pgdat)
{
	build_zonelists(NODE_DATA(0));
	per_cpu_pages_init(&per_cpu(boot_pageset, 0),
			   &per_cpu(boot_zonestats, 0));
}

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

		if (IS_ALIGNED(pfn, pageblock_nr_pages)) {
			int mt = (migratetype < MIGRATE_PCPTYPES) ?
					 MIGRATE_UNMOVABLE :
					 migratetype;
			set_pfnblock_flags_mask(page, (unsigned long)mt, pfn,
						MIGRATETYPE_MASK);
			cond_resched();
		}
		pfn++;
	}
}

static void __init init_unavailable_range(unsigned long spfn,
					  unsigned long epfn, int zone,
					  int node)
{
	unsigned long pfn;

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

static void __meminit setup_zone_pageset(struct zone *zone)
{
	struct per_cpu_pages *pcp;
	struct per_cpu_zonestat *pzstats;

	zone->per_cpu_zonestats = alloc_percpu(struct per_cpu_zonestat);
	zone->per_cpu_pageset = alloc_percpu(struct per_cpu_pages);
	pcp = per_cpu_ptr(zone->per_cpu_pageset, 0);
	pzstats = per_cpu_ptr(zone->per_cpu_zonestats, 0);
	per_cpu_pages_init(pcp, pzstats);
	WRITE_ONCE(pcp->batch, 1);
}

void __init setup_per_cpu_pageset(void)
{
	struct zone *zone;

	for_each_populated_zone(zone)
		setup_zone_pageset(zone);
}

static void __meminit init_currently_empty_zone(struct zone *zone,
						unsigned long zone_start_pfn,
						unsigned long size)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	int zone_idx = zone_idx(zone) + 1;

	if (zone_idx > pgdat->nr_zones)
		pgdat->nr_zones = zone_idx;

	zone->zone_start_pfn = zone_start_pfn;

	{
		unsigned int order, t;
		for_each_migratetype_order(order, t) {
			INIT_LIST_HEAD(&zone->free_area[order].free_list[t]);
			zone->free_area[order].nr_free = 0;
		}
	}
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

	if (node_online(0)) {
		pg_data_t *pgdat = NODE_DATA(0);
		unsigned long node_start_pfn = -1UL;
		unsigned long node_end_pfn = 0;
		unsigned long this_start_pfn, this_end_pfn;
		int j;

		for_each_mem_pfn_range(j, 0, &this_start_pfn, &this_end_pfn,
				       NULL) {
			node_start_pfn = min(node_start_pfn, this_start_pfn);
			node_end_pfn = max(node_end_pfn, this_end_pfn);
		}
		if (node_start_pfn == -1UL)
			node_start_pfn = 0;

		pgdat->node_id = 0;
		pgdat->node_start_pfn = node_start_pfn;

		{
			unsigned long totalpages = 0;
			enum zone_type zi;

			for (zi = 0; zi < MAX_NR_ZONES; zi++) {
				struct zone *zone = pgdat->node_zones + zi;
				unsigned long z_start_pfn, z_end_pfn;
				unsigned long zone_low =
					arch_zone_lowest_possible_pfn[zi];
				unsigned long zone_high =
					arch_zone_highest_possible_pfn[zi];
				unsigned long zsize;

				if (!node_start_pfn && !node_end_pfn) {
					zone->zone_start_pfn = 0;
					zone->spanned_pages = 0;
					zone->present_pages = 0;
					continue;
				}
				z_start_pfn = clamp(node_start_pfn, zone_low,
						    zone_high);
				z_end_pfn = clamp(node_end_pfn, zone_low,
						  zone_high);
				z_end_pfn = min(z_end_pfn, node_end_pfn);
				z_start_pfn = max(z_start_pfn, node_start_pfn);
				zsize = z_end_pfn - z_start_pfn;

				zone->zone_start_pfn = zsize ? z_start_pfn : 0;
				zone->spanned_pages = zsize;
				zone->present_pages = zsize;
				totalpages += zsize;
			}

			pgdat->node_spanned_pages = totalpages;
			pgdat->node_present_pages = totalpages;
		}

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
				map = memblock_alloc_try_nid_raw(
					map_size, SMP_CACHE_BYTES,
					MEMBLOCK_LOW_LIMIT,
					MEMBLOCK_ALLOC_ACCESSIBLE,
					pgdat->node_id);
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
