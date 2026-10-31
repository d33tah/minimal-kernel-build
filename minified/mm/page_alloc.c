
#include <linux/memblock.h>

#include <linux/sched/mm.h>
#include <asm/div64.h>

#include "internal.h"

/*
 * Buddy page allocator for one node with one populated zone: one free list
 * per order, no per-cpu page lists, no migratetype grouping and no
 * watermarks (there is no reclaim to wake). Blocks are split on allocation
 * and coalesced with their free buddies on free.
 */

atomic_long_t _totalram_pages __read_mostly;
gfp_t gfp_allowed_mask __read_mostly = GFP_BOOT_MASK;

static char *const zone_names[MAX_NR_ZONES] = {
	"Normal",
};

static unsigned long nr_kernel_pages __initdata;
static unsigned long nr_all_pages __initdata;

static unsigned long arch_zone_lowest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long arch_zone_highest_possible_pfn[MAX_NR_ZONES] __initdata;

/*
 * NODES_SHIFT is a hardcoded 0 in this tree (include/linux/numa.h), so
 * MAX_NUMNODES == (1 << 0) == 1 and the `#if MAX_NUMNODES > 1` out-of-line
 * `nr_node_ids` definition was statically dead in every TU. nr_node_ids is
 * served unconditionally by `#define nr_node_ids 1U` (include/linux/nodemask.h).
 */

static void prep_compound_head(struct page *page, unsigned int order)
{
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

static inline void set_buddy_order(struct page *page, unsigned int order)
{
	set_page_private(page, order);
	__SetPageBuddy(page);
}

static inline void add_to_free_list(struct page *page, struct zone *zone,
				    unsigned int order, bool tail)
{
	struct free_area *area = &zone->free_area[order];

	if (tail)
		list_add_tail(&page->lru, &area->free_list);
	else
		list_add(&page->lru, &area->free_list);
}

static inline void del_page_from_free_list(struct page *page, struct zone *zone,
					   unsigned int order)
{
	list_del(&page->lru);
	__ClearPageBuddy(page);
	set_page_private(page, 0);
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

/*
 * Freeing function for a buddy system allocator.
 *
 * The concept of a buddy system is to maintain direct-mapped table
 * (containing bit values) for memory blocks of various "orders".
 * The bottom level table contains the map for the smallest allocatable
 * units of memory (here, pages), and each level above it describes
 * pairs of units from the levels below, hence, "buddies".
 * At a high level, all that happens here is marking the table entry
 * at the bottom level available, and propagating the changes upward
 * as necessary, plus some accounting needed to play nicely with other
 * parts of the VM system.
 * At each level, we keep a list of pages, which are heads of continuous
 * free pages of length of (1 << order) and marked with PageBuddy.
 * Page's order is recorded in page_private(page) field.
 * So when we are allocating or freeing one, we can derive the state of the
 * other.  That is, if we allocate a small block, and both were
 * free, the remainder of the region must be split into blocks.
 * If a block is freed, and its buddy is also free, then this
 * triggers coalescing into a block of larger size.
 *
 * -- nyc
 */

static inline void __free_one_page(struct page *page, unsigned long pfn,
				   struct zone *zone, unsigned int order,
				   bool to_tail)
{
	unsigned long buddy_pfn;
	unsigned long combined_pfn;
	struct page *buddy;

	__mod_zone_page_state(zone, NR_FREE_PAGES, 1 << order);

	while (order < MAX_ORDER - 1) {
		buddy = find_buddy_page_pfn(page, pfn, order, &buddy_pfn);
		if (!buddy)
			goto done_merging;

		/*
		 * Our buddy is free, merge with it and move up one order.
		 */
		del_page_from_free_list(buddy, zone, order);
		combined_pfn = buddy_pfn & pfn;
		page = page + (combined_pfn - pfn);
		pfn = combined_pfn;
		order++;
	}

done_merging:
	set_buddy_order(page, order);

	add_to_free_list(page, zone, order, to_tail);
}

static void free_tail_pages_check(struct page *head_page, struct page *page)
{
	page->mapping = NULL;
	clear_compound_head(page);
}

static __always_inline void free_pages_prepare(struct page *page,
					       unsigned int order)
{
	/*
	 * Check tail pages before head page information is cleared to
	 * avoid checking PageCompound for order-0 pages.
	 */
	if (unlikely(order)) {
		bool compound = PageCompound(page);
		int i;

		for (i = 1; i < (1 << order); i++) {
			if (compound)
				free_tail_pages_check(page, page + i);
			(page + i)->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
		}
	}
	if (PageMappingFlags(page))
		page->mapping = NULL;
	page->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
}

static void __free_pages_ok(struct page *page, unsigned int order, bool tail)
{
	unsigned long flags;
	unsigned long pfn = page_to_pfn(page);
	struct zone *zone = page_zone(page);

	free_pages_prepare(page, order);

	spin_lock_irqsave(&zone->lock, flags);
	__free_one_page(page, pfn, zone, order, tail);
	spin_unlock_irqrestore(&zone->lock, flags);
}

static void __free_pages_core(struct page *page, unsigned int order)
{
	unsigned int nr_pages = 1 << order;
	struct page *p = page;
	unsigned int loop;

	for (loop = 0; loop < (nr_pages - 1); loop++, p++) {
		__ClearPageReserved(p);
		set_page_count(p, 0);
	}
	__ClearPageReserved(p);
	set_page_count(p, 0);

	__free_pages_ok(page, order, true);
}

void __init memblock_free_pages(struct page *page, unsigned long pfn,
				unsigned int order)
{
	__free_pages_core(page, order);
}

static void prep_new_page(struct page *page, unsigned int order,
			  gfp_t gfp_flags)
{
	set_page_private(page, 0);
	set_page_refcounted(page);

	/* No highmem: every page has a kernel mapping. */
	if (gfp_flags & __GFP_ZERO)
		memset(page_address(page), 0, PAGE_SIZE << order);

	if (order && (gfp_flags & __GFP_COMP)) {
		int i, nr_pages = 1 << order;

		__SetPageHead(page);
		for (i = 1; i < nr_pages; i++)
			prep_compound_tail(page, i);
		prep_compound_head(page, order);
	}
}

static struct page *rmqueue(struct zone *zone, unsigned int order)
{
	unsigned int current_order;
	struct page *page = NULL;
	unsigned long flags;

	spin_lock_irqsave(&zone->lock, flags);
	for (current_order = order; current_order < MAX_ORDER;
	     ++current_order) {
		unsigned int high = current_order;
		unsigned long size = 1 << high;

		page = list_first_entry_or_null(
			&zone->free_area[current_order].free_list, struct page,
			lru);
		if (!page)
			continue;
		del_page_from_free_list(page, zone, current_order);
		/* split the buddy block down to @order */
		while (high > order) {
			high--;
			size >>= 1;
			add_to_free_list(&page[size], zone, high, false);
			set_buddy_order(&page[size], high);
		}
		__mod_zone_page_state(zone, NR_FREE_PAGES, -(1 << order));
		break;
	}
	spin_unlock_irqrestore(&zone->lock, flags);
	return page;
}

struct page *__alloc_pages(gfp_t gfp, unsigned int order, int preferred_nid)
{
	struct zone *zone = &NODE_DATA(preferred_nid)->node_zones[ZONE_NORMAL];
	struct page *page;

	if (WARN_ON_ONCE_GFP(order >= MAX_ORDER, gfp))
		return NULL;

	gfp &= gfp_allowed_mask;
	gfp = current_gfp_context(gfp);

	page = rmqueue(zone, order);
	if (page)
		prep_new_page(page, order, gfp);
	return page;
}

struct folio *__folio_alloc(gfp_t gfp, unsigned int order, int preferred_nid)
{
	struct page *page =
		__alloc_pages(gfp | __GFP_COMP, order, preferred_nid);

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

void __free_pages(struct page *page, unsigned int order)
{
	if (put_page_testzero(page))
		__free_pages_ok(page, order, false);
	else if (!PageHead(page))
		while (order-- > 0)
			__free_pages_ok(page + (1 << order), order, false);
}

/*
 * The last reference to a refcounted page is gone (folio_put/put_page): give
 * it back to the free lists. Page cache folios hold their own reference
 * (__filemap_add_folio) and mapped pages keep the one taken at fault time, so
 * this only runs for pages nothing indexes or maps any more.
 */
void __put_page(struct page *page)
{
	__free_pages_ok(page, compound_order(page), false);
}

void free_pages(unsigned long addr, unsigned int order)
{
	if (addr != 0) {
		__free_pages(virt_to_page((void *)addr), order);
	}
}

static void __meminit memmap_init_range(unsigned long size, int nid,
					unsigned long zone,
					unsigned long start_pfn)
{
	unsigned long pfn, end_pfn = start_pfn + size;
	struct page *page;

	if (highest_memmap_pfn < end_pfn - 1)
		highest_memmap_pfn = end_pfn - 1;

	for (pfn = start_pfn; pfn < end_pfn;) {
		page = pfn_to_page(pfn);
		__init_single_page(page, pfn, zone, nid);
		pfn++;
	}
}

static void __meminit zone_init_free_lists(struct zone *zone)
{
	unsigned int order;

	for (order = 0; order < MAX_ORDER; order++) {
		INIT_LIST_HEAD(&zone->free_area[order].free_list);
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

	memmap_init_range(end_pfn - start_pfn, nid, zone_id, start_pfn);

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

static void __meminit init_currently_empty_zone(struct zone *zone,
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
	}

	pgdat->node_spanned_pages = totalpages;
}

/*
 * managed_pages starts at 0: __free_pages_core() adds every page memblock
 * releases to the buddy allocator, so that is the only count.
 */
static void __meminit zone_init_internals(struct zone *zone, enum zone_type idx,
					  int nid)
{
	zone->name = zone_names[idx];
	zone->zone_pgdat = NODE_DATA(nid);
	spin_lock_init(&zone->lock);
}

static unsigned long __init calc_memmap_size(unsigned long spanned_pages,
					     unsigned long present_pages)
{
	unsigned long pages = spanned_pages;

	return PAGE_ALIGN(pages * sizeof(struct page)) >> PAGE_SHIFT;
}

static void __init free_area_init_core(struct pglist_data *pgdat)
{
	enum zone_type j;
	int nid = pgdat->node_id;

	for (j = 0; j < MAX_NR_ZONES; j++) {
		struct zone *zone = pgdat->node_zones + j;
		unsigned long size, freesize, memmap_pages;

		size = zone->spanned_pages;
		freesize = zone->present_pages;

		/*
		 * Adjust freesize so that it accounts for how much memory
		 * is used by this zone for memmap. This affects the watermark
		 * and per-cpu initialisations
		 */
		memmap_pages = calc_memmap_size(size, freesize);
		if (freesize >= memmap_pages) {
			freesize -= memmap_pages;
		}

		nr_kernel_pages += freesize;
		nr_all_pages += freesize;

		zone_init_internals(zone, j, nid);

		if (!size)
			continue;

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
		size = (end - start) * sizeof(struct page);
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

	memset(arch_zone_lowest_possible_pfn, 0,
	       sizeof(arch_zone_lowest_possible_pfn));
	memset(arch_zone_highest_possible_pfn, 0,
	       sizeof(arch_zone_highest_possible_pfn));

	start_pfn = find_min_pfn_with_active_regions();

	/* Simple linear zone setup without movable zones */
	for (i = 0; i < MAX_NR_ZONES; i++) {
		end_pfn = max(max_zone_pfn[i], start_pfn);
		arch_zone_lowest_possible_pfn[i] = start_pfn;
		arch_zone_highest_possible_pfn[i] = end_pfn;
		start_pfn = end_pfn;
	}

	/* Basic node initialization (single node, MAX_NUMNODES=1) */
	free_area_init_node(0);

	memmap_init();
}

void *__init alloc_large_system_hash(const char *tablename,
				     unsigned long bucketsize,
				     unsigned long numentries, int scale,
				     int flags, unsigned int *_hash_shift,
				     unsigned int *_hash_mask,
				     unsigned long low_limit,
				     unsigned long high_limit)
{
	unsigned long long max = high_limit;
	unsigned long log2qty, size;
	void *table = NULL;

	/* allow the kernel cmdline to have a say */
	if (!numentries) {
		/* round applicable memory size up to nearest megabyte */
		numentries = nr_kernel_pages;

		/* It isn't necessary when PAGE_SIZE >= 1MB */
		if (PAGE_SHIFT < 20)
			numentries =
				round_up(numentries, (1 << 20) / PAGE_SIZE);

		/* limit to 1 bucket per 2^scale bytes of low memory */
		if (scale > PAGE_SHIFT)
			numentries >>= (scale - PAGE_SHIFT);
		else
			numentries <<= (PAGE_SHIFT - scale);

		/* Make sure we've got at least a 0-order allocation.. */
		if (unlikely((numentries * bucketsize) < PAGE_SIZE))
			numentries = PAGE_SIZE / bucketsize;
	}
	numentries = roundup_pow_of_two(numentries);

	/* limit allocation size to 1/16 total memory by default */
	if (max == 0) {
		max = ((unsigned long long)nr_all_pages << PAGE_SHIFT) >> 4;
		do_div(max, bucketsize);
	}
	max = min(max, 0x80000000ULL);

	if (numentries < low_limit)
		numentries = low_limit;
	if (numentries > max)
		numentries = max;

	log2qty = ilog2(numentries);

	/*
	 * The only runtime caller (dcache_init_early) always passes HASH_EARLY
	 * | HASH_ZERO and hashdist is the compile-time constant 0, so only the
	 * zeroed memblock arm is reachable.
	 */
	do {
		size = bucketsize << log2qty;
		table = memblock_alloc(size, SMP_CACHE_BYTES);
	} while (!table && size > PAGE_SIZE && --log2qty);

	if (!table)
		panic("Failed to allocate %s hash table\n", tablename);

	if (_hash_shift)
		*_hash_shift = log2qty;
	if (_hash_mask)
		*_hash_mask = (1 << log2qty) - 1;

	return table;
}
