 

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
#include <linux/kasan.h>
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
#include <linux/cpuset.h>
#include <linux/memory_hotplug.h>
#include <linux/nodemask.h>
#include <linux/vmalloc.h>
#include <linux/vmstat.h>
#include <linux/mempolicy.h>
#include <linux/memremap.h>
#include <linux/stop_machine.h>
#include <linux/random.h>
#include <linux/sort.h>
#include <linux/pfn.h>
#include <linux/backing-dev.h>
#include <linux/fault-inject.h>
#include <linux/page-isolation.h>
#include <linux/kmemleak.h>
#include <linux/compaction.h>

#include <linux/mm_inline.h>
#include <linux/mmu_notifier.h>
#include <linux/migrate.h>
#include <linux/hugetlb.h>
#include <linux/sched/rt.h>
#include <linux/sched/mm.h>
#include <linux/page_owner.h>
#include <linux/page_table_check.h>
#include <linux/kthread.h>
#include <linux/memcontrol.h>
#include <linux/lockdep.h>
#include <linux/nmi.h>
#include <linux/psi.h>
#include <linux/padata.h>
#include <linux/khugepaged.h>
#include <linux/buffer_head.h>
#include <linux/delayacct.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/div64.h>
#include "internal.h"
#include "shuffle.h"
#include "page_reporting.h"
#include "swap.h"

typedef int __bitwise fpi_t;

#define FPI_NONE		((__force fpi_t)0)

#define FPI_SKIP_REPORT_NOTIFY	((__force fpi_t)BIT(0))

#define FPI_TO_TAIL		((__force fpi_t)BIT(1))

#define FPI_SKIP_KASAN_POISON	((__force fpi_t)BIT(2))

static DEFINE_MUTEX(pcp_batch_high_lock);
#define MIN_PERCPU_PAGELIST_HIGH_FRACTION (8)

struct pagesets {
	local_lock_t lock;
};
static DEFINE_PER_CPU(struct pagesets, pagesets) = {
	.lock = INIT_LOCAL_LOCK(lock),
};

DEFINE_STATIC_KEY_TRUE(vm_numa_stat_key);

static DEFINE_MUTEX(pcpu_drain_mutex);

nodemask_t node_states[NR_NODE_STATES] __read_mostly = {
	[N_POSSIBLE] = NODE_MASK_ALL,
	[N_ONLINE] = { { [0] = 1UL } },
	[N_NORMAL_MEMORY] = { { [0] = 1UL } },
	[N_MEMORY] = { { [0] = 1UL } },
	[N_CPU] = { { [0] = 1UL } },
};

atomic_long_t _totalram_pages __read_mostly;
unsigned long totalreserve_pages __read_mostly;
unsigned long totalcma_pages __read_mostly;

int percpu_pagelist_high_fraction;
gfp_t gfp_allowed_mask __read_mostly = GFP_BOOT_MASK;
DEFINE_STATIC_KEY_MAYBE(CONFIG_INIT_ON_ALLOC_DEFAULT_ON, init_on_alloc);

DEFINE_STATIC_KEY_MAYBE(CONFIG_INIT_ON_FREE_DEFAULT_ON, init_on_free);

static int __init early_init_on_alloc(char *buf) { return 0; }
early_param("init_on_alloc", early_init_on_alloc);

static int __init early_init_on_free(char *buf) { return 0; }
early_param("init_on_free", early_init_on_free);

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

int sysctl_lowmem_reserve_ratio[MAX_NR_ZONES] = {
	[ZONE_NORMAL] = 32,
	[ZONE_MOVABLE] = 0,
};

static char * const zone_names[MAX_NR_ZONES] = {
	 "Normal",
	 "Movable",
};

const char * const migratetype_names[MIGRATE_TYPES] = {
	"Unmovable",
	"Movable",
	"Reclaimable",
	"HighAtomic",
};

compound_page_dtor * const compound_page_dtors[NR_COMPOUND_DTORS] = {
	[NULL_COMPOUND_DTOR] = NULL,
	[COMPOUND_PAGE_DTOR] = free_compound_page,
};

int min_free_kbytes = 1024;
int user_min_free_kbytes = -1;
int watermark_boost_factor __read_mostly = 15000;
int watermark_scale_factor = 10;

static unsigned long nr_kernel_pages __initdata;
static unsigned long nr_all_pages __initdata;
static unsigned long dma_reserve __initdata;

static unsigned long arch_zone_lowest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long arch_zone_highest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long zone_movable_pfn[MAX_NUMNODES] __initdata;
static bool mirrored_kernelcore __meminitdata;

int movable_zone;

#if MAX_NUMNODES > 1
unsigned int nr_node_ids __read_mostly = MAX_NUMNODES;
unsigned int nr_online_nodes __read_mostly = 1;
#endif

int page_group_by_mobility_disabled __read_mostly;

static inline bool early_page_uninitialised(unsigned long pfn)
{
	return false;
}

static inline bool defer_init(int nid, unsigned long pfn, unsigned long end_pfn)
{
	return false;
}

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

unsigned long get_pfnblock_flags_mask(const struct page *page,
					unsigned long pfn, unsigned long mask)
{
	return __get_pfnblock_flags_mask(page, pfn, mask);
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

void set_pageblock_migratetype(struct page *page, int migratetype)
{
	if (unlikely(page_group_by_mobility_disabled &&
		     migratetype < MIGRATE_PCPTYPES))
		migratetype = MIGRATE_UNMOVABLE;

	set_pfnblock_flags_mask(page, (unsigned long)migratetype,
				page_to_pfn(page), MIGRATETYPE_MASK);
}

static inline int __maybe_unused bad_range(struct zone *zone, struct page *page)
{
	return 0;
}


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
	mem_cgroup_uncharge(page_folio(page));
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

void prep_compound_page(struct page *page, unsigned int order)
{
	int i;
	int nr_pages = 1 << order;

	__SetPageHead(page);
	for (i = 1; i < nr_pages; i++)
		prep_compound_tail(page, i);

	prep_compound_head(page, order);
}

static inline bool set_page_guard(struct zone *zone, struct page *page,
			unsigned int order, int migratetype) { return false; }

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
	
	if (page_reported(page))
		__ClearPageReported(page);

	list_del(&page->lru);
	__ClearPageBuddy(page);
	set_page_private(page, 0);
	zone->free_area[order].nr_free--;
}

static inline bool
buddy_merge_likely(unsigned long pfn, unsigned long buddy_pfn,
		   struct page *page, unsigned int order)
{
	unsigned long higher_page_pfn;
	struct page *higher_page;

	if (order >= MAX_ORDER - 2)
		return false;

	higher_page_pfn = buddy_pfn & pfn;
	higher_page = page + (higher_page_pfn - pfn);

	return find_buddy_page_pfn(higher_page, higher_page_pfn, order + 1,
			NULL) != NULL;
}

static inline void __free_one_page(struct page *page,
		unsigned long pfn,
		struct zone *zone, unsigned int order,
		int migratetype, fpi_t fpi_flags)
{
	/* Simplified buddy allocator: just add to free list without merging */
	if (likely(!is_migrate_isolate(migratetype)))
		__mod_zone_freepage_state(zone, 1 << order, migratetype);

	set_buddy_order(page, order);

	if (fpi_flags & FPI_TO_TAIL)
		add_to_free_list_tail(page, zone, order, migratetype);
	else
		add_to_free_list(page, zone, order, migratetype);

	if (!(fpi_flags & FPI_SKIP_REPORT_NOTIFY))
		page_reporting_notify_free(order);
}

int split_free_page(struct page *free_page,
			unsigned int order, unsigned long split_pfn_offset)
{
	/* Stub: free page splitting not needed for minimal kernel */
	return -ENOENT;
}

static inline bool page_expected_state(struct page *page,
					unsigned long check_flags)
{
	if (unlikely(atomic_read(&page->_mapcount) != -1))
		return false;

	if (unlikely((unsigned long)page->mapping |
			page_ref_count(page) |
			(page->flags & check_flags)))
		return false;

	return true;
}


static __always_inline bool free_pages_prepare(struct page *page,
			unsigned int order, bool check_free, fpi_t fpi_flags)
{
	/* Stub: minimal page freeing for simple system */
	if (PageMappingFlags(page))
		page->mapping = NULL;
	if (memcg_kmem_enabled() && PageMemcgKmem(page))
		__memcg_kmem_uncharge_page(page, order);

	page->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
	reset_page_owner(page, order);
	page_table_check_free(page, order);

	arch_free_page(page, order);
	return true;
}

static bool free_pcp_prepare(struct page *page, unsigned int order)
{
	if (debug_pagealloc_enabled_static())
		return free_pages_prepare(page, order, true, FPI_NONE);
	else
		return free_pages_prepare(page, order, false, FPI_NONE);
}


static void free_pcppages_bulk(struct zone *zone, int count,
					struct per_cpu_pages *pcp,
					int pindex)
{
	/* Stub: per-CPU page caching optimization not needed for minimal kernel */
}

static void free_one_page(struct zone *zone,
				struct page *page, unsigned long pfn,
				unsigned int order,
				int migratetype, fpi_t fpi_flags)
{
	unsigned long flags;

	spin_lock_irqsave(&zone->lock, flags);
	if (unlikely(has_isolate_pageblock(zone) ||
		is_migrate_isolate(migratetype))) {
		migratetype = get_pfnblock_migratetype(page, pfn);
	}
	__free_one_page(page, pfn, zone, order, migratetype, fpi_flags);
	spin_unlock_irqrestore(&zone->lock, flags);
}

static void __meminit __init_single_page(struct page *page, unsigned long pfn,
				unsigned long zone, int nid)
{
	mm_zero_struct_page(page);
	set_page_links(page, zone, nid, pfn);
	init_page_count(page);
	page_mapcount_reset(page);
	page_cpupid_reset_last(page);
	page_kasan_tag_reset(page);

	INIT_LIST_HEAD(&page->lru);
#ifdef WANT_PAGE_VIRTUAL
	
	if (!is_highmem_idx(zone))
		set_page_address(page, __va(pfn << PAGE_SHIFT));
#endif
}

static inline void init_reserved_page(unsigned long pfn)
{
}

void __meminit reserve_bootmem_region(phys_addr_t start, phys_addr_t end)
{
	unsigned long start_pfn = PFN_DOWN(start);
	unsigned long end_pfn = PFN_UP(end);

	for (; start_pfn < end_pfn; start_pfn++) {
		if (pfn_valid(start_pfn)) {
			struct page *page = pfn_to_page(start_pfn);

			init_reserved_page(start_pfn);

			
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

	migratetype = get_pfnblock_migratetype(page, pfn);

	spin_lock_irqsave(&zone->lock, flags);
	if (unlikely(has_isolate_pageblock(zone) ||
		is_migrate_isolate(migratetype))) {
		migratetype = get_pfnblock_migratetype(page, pfn);
	}
	__free_one_page(page, pfn, zone, order, migratetype, fpi_flags);
	spin_unlock_irqrestore(&zone->lock, flags);

	__count_vm_events(PGFREE, 1 << order);
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
	if (early_page_uninitialised(pfn))
		return;
	__free_pages_core(page, order);
}

struct page *__pageblock_pfn_to_page(unsigned long start_pfn,
				     unsigned long end_pfn, struct zone *zone)
{
	struct page *start_page;
	struct page *end_page;

	
	end_pfn--;

	if (!pfn_valid(start_pfn) || !pfn_valid(end_pfn))
		return NULL;

	start_page = pfn_to_online_page(start_pfn);
	if (!start_page)
		return NULL;

	if (page_zone(start_page) != zone)
		return NULL;

	end_page = pfn_to_page(end_pfn);

	
	if (page_zone_id(start_page) != page_zone_id(end_page))
		return NULL;

	return start_page;
}

void set_zone_contiguous(struct zone *zone)
{
	/* Stub: assume contiguous zones for minimal system */
	zone->contiguous = true;
}

void clear_zone_contiguous(struct zone *zone)
{
	zone->contiguous = false;
}

void __init page_alloc_init_late(void)
{
	struct zone *zone;

	buffer_init();
	memblock_discard();

	/* Stub: skip memory shuffling for minimal system */
	for_each_populated_zone(zone)
		set_zone_contiguous(zone);
}

static inline void expand(struct zone *zone, struct page *page,
	int low, int high, int migratetype)
{
	unsigned long size = 1 << high;

	while (high > low) {
		high--;
		size >>= 1;
		VM_BUG_ON_PAGE(bad_range(zone, &page[size]), &page[size]);

		
		if (set_page_guard(zone, &page[size], high, migratetype))
			continue;

		add_to_free_list(&page[size], zone, high, migratetype);
		set_buddy_order(&page[size], high);
	}
}

static void check_new_page_bad(struct page *page)
{
	/* Stub: page validation not needed for minimal kernel */
}

static inline int check_new_page(struct page *page)
{
	if (likely(page_expected_state(page,
				PAGE_FLAGS_CHECK_AT_PREP|__PG_HWPOISON)))
		return 0;

	check_new_page_bad(page);
	return 1;
}

static bool check_new_pages(struct page *page, unsigned int order)
{
	/* Stub: skip page checking for minimal kernel */
	return false;
}

static inline bool check_pcp_refill(struct page *page, unsigned int order)
{
	return check_new_pages(page, order);
}
static inline bool check_new_pcp(struct page *page, unsigned int order)
{
	/* Stub: skip page checking for minimal kernel */
	return false;
}

static inline bool should_skip_kasan_unpoison(gfp_t flags, bool init_tags)
{
	/* Stub: KASAN not needed for minimal kernel */
	return true;
}

static inline bool should_skip_init(gfp_t flags)
{
	/* Stub: advanced initialization not needed for minimal kernel */
	return false;
}

inline void post_alloc_hook(struct page *page, unsigned int order,
				gfp_t gfp_flags)
{
	/* Stub: minimal post-allocation setup */
	set_page_private(page, 0);
	set_page_refcounted(page);
	arch_alloc_page(page, order);
	set_page_owner(page, order, gfp_flags);
	page_table_check_alloc(page, order);
}

static void prep_new_page(struct page *page, unsigned int order, gfp_t gfp_flags,
							unsigned int alloc_flags)
{
	post_alloc_hook(page, order, gfp_flags);

	if (order && (gfp_flags & __GFP_COMP))
		prep_compound_page(page, order);

	
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
		area = &(zone->free_area[current_order]);
		page = get_page_from_free_area(area, migratetype);
		if (!page)
			continue;
		del_page_from_free_list(page, zone, current_order);
		expand(zone, page, order, current_order, migratetype);
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

static inline struct page *__rmqueue_cma_fallback(struct zone *zone,
					unsigned int order) { return NULL; }

static int move_freepages(struct zone *zone,
			  unsigned long start_pfn, unsigned long end_pfn,
			  int migratetype, int *num_movable)
{
	struct page *page;
	unsigned long pfn;
	unsigned int order;
	int pages_moved = 0;

	/* Stub: skip num_movable tracking for minimal kernel */
	if (num_movable)
		*num_movable = 0;

	for (pfn = start_pfn; pfn <= end_pfn;) {
		page = pfn_to_page(pfn);
		if (!PageBuddy(page)) {
			pfn++;
			continue;
		}

		VM_BUG_ON_PAGE(page_to_nid(page) != zone_to_nid(zone), page);
		VM_BUG_ON_PAGE(page_zone(page) != zone, page);

		order = buddy_order(page);
		move_to_free_list(page, zone, order, migratetype);
		pfn += 1 << order;
		pages_moved += 1 << order;
	}

	return pages_moved;
}

int move_freepages_block(struct zone *zone, struct page *page,
				int migratetype, int *num_movable)
{
	/* Stub: freepage block moving not used in minimal kernel */
	if (num_movable)
		*num_movable = 0;
	return 0;
}


static bool can_steal_fallback(unsigned int order, int start_mt)
{
	/* Stub: always allow fallback for minimal kernel */
	return true;
}

static void steal_suitable_fallback(struct zone *zone, struct page *page,
		unsigned int alloc_flags, int start_type, bool whole_block)
{
	/* Minimal stub: just move page to target type */
	unsigned int current_order = buddy_order(page);
	move_to_free_list(page, zone, current_order, start_type);
}

int find_suitable_fallback(struct free_area *area, unsigned int order,
			int migratetype, bool only_stealable, bool *can_steal)
{
	int i;
	int fallback_mt;

	if (area->nr_free == 0)
		return -1;

	*can_steal = false;
	for (i = 0;; i++) {
		fallback_mt = fallbacks[migratetype][i];
		if (fallback_mt == MIGRATE_TYPES)
			break;

		if (free_area_empty(area, fallback_mt))
			continue;

		if (can_steal_fallback(order, migratetype))
			*can_steal = true;

		if (!only_stealable)
			return fallback_mt;

		if (*can_steal)
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
	bool can_steal;
	struct page *page;

	for (current_order = MAX_ORDER - 1; current_order >= order; --current_order) {
		area = &(zone->free_area[current_order]);
		fallback_mt = find_suitable_fallback(area, current_order,
				start_migratetype, false, &can_steal);
		if (fallback_mt == -1)
			continue;

		page = get_page_from_free_area(area, fallback_mt);
		steal_suitable_fallback(zone, page, alloc_flags, start_migratetype, can_steal);
		return true;
	}

	return false;
}

static __always_inline struct page *
__rmqueue(struct zone *zone, unsigned int order, int migratetype,
						unsigned int alloc_flags)
{
	struct page *page;

	if (IS_ENABLED(CONFIG_CMA)) {
		
		if (alloc_flags & ALLOC_CMA &&
		    zone_page_state(zone, NR_FREE_CMA_PAGES) >
		    zone_page_state(zone, NR_FREE_PAGES) / 2) {
			page = __rmqueue_cma_fallback(zone, order);
			if (page)
				return page;
		}
	}
retry:
	page = __rmqueue_smallest(zone, order, migratetype);
	if (unlikely(!page)) {
		if (alloc_flags & ALLOC_CMA)
			page = __rmqueue_cma_fallback(zone, order);

		if (!page && __rmqueue_fallback(zone, order, migratetype,
								alloc_flags))
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

		if (unlikely(check_pcp_refill(page, order)))
			continue;

		
		list_add_tail(&page->lru, list);
		allocated++;
		if (is_migrate_cma(get_pcppage_migratetype(page)))
			__mod_zone_page_state(zone, NR_FREE_CMA_PAGES,
					      -(1 << order));
	}

	
	__mod_zone_page_state(zone, NR_FREE_PAGES, -(i << order));
	spin_unlock(&zone->lock);
	return allocated;
}


static void drain_pages(unsigned int cpu)
{
	/* Stub: skip per-CPU page draining for minimal kernel */
}

void drain_local_pages(struct zone *zone)
{
	/* Stub: skip page draining for minimal kernel */
}


static void __drain_all_pages(struct zone *zone, bool force_all_cpus)
{
	/* Minimal stub: skip complex per-CPU draining */
	if (!mm_percpu_wq)
		return;
}

void drain_all_pages(struct zone *zone)
{
	__drain_all_pages(zone, false);
}

static bool free_unref_page_prepare(struct page *page, unsigned long pfn,
							unsigned int order)
{
	int migratetype;

	if (!free_pcp_prepare(page, order))
		return false;

	migratetype = get_pfnblock_migratetype(page, pfn);
	set_pcppage_migratetype(page, migratetype);
	return true;
}

static int nr_pcp_free(struct per_cpu_pages *pcp, int high, int batch,
		       bool free_high)
{
	/* Stub: simple PCP freeing for minimal kernel */
	return free_high ? pcp->count : batch;
}

static int nr_pcp_high(struct per_cpu_pages *pcp, struct zone *zone,
		       bool free_high)
{
	/* Stub: simple PCP high watermark for minimal kernel */
	return free_high ? 0 : READ_ONCE(pcp->high);
}

static void free_unref_page_commit(struct page *page, int migratetype,
				   unsigned int order)
{
	struct zone *zone = page_zone(page);
	struct per_cpu_pages *pcp;
	int high;
	int pindex;
	bool free_high;

	__count_vm_event(PGFREE);
	pcp = this_cpu_ptr(zone->per_cpu_pageset);
	pindex = order_to_pindex(migratetype, order);
	list_add(&page->lru, &pcp->lists[pindex]);
	pcp->count += 1 << order;

	
	free_high = (pcp->free_factor && order && order <= PAGE_ALLOC_COSTLY_ORDER);

	high = nr_pcp_high(pcp, zone, free_high);
	if (pcp->count >= high) {
		int batch = READ_ONCE(pcp->batch);

		free_pcppages_bulk(zone, nr_pcp_free(pcp, high, batch, free_high), pcp, pindex);
	}
}

void free_unref_page(struct page *page, unsigned int order)
{
	unsigned long flags;
	unsigned long pfn = page_to_pfn(page);
	int migratetype;

	if (!free_unref_page_prepare(page, pfn, order))
		return;

	
	migratetype = get_pcppage_migratetype(page);
	if (unlikely(migratetype >= MIGRATE_PCPTYPES)) {
		if (unlikely(is_migrate_isolate(migratetype))) {
			free_one_page(page_zone(page), page, pfn, order, migratetype, FPI_NONE);
			return;
		}
		migratetype = MIGRATE_MOVABLE;
	}

	local_lock_irqsave(&pagesets.lock, flags);
	free_unref_page_commit(page, migratetype, order);
	local_unlock_irqrestore(&pagesets.lock, flags);
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

		
		migratetype = get_pcppage_migratetype(page);
		if (unlikely(is_migrate_isolate(migratetype))) {
			list_del(&page->lru);
			free_one_page(page_zone(page), page, pfn, 0, migratetype, FPI_NONE);
			continue;
		}
	}

	local_lock_irqsave(&pagesets.lock, flags);
	list_for_each_entry_safe(page, next, list, lru) {
		
		migratetype = get_pcppage_migratetype(page);
		if (unlikely(migratetype >= MIGRATE_PCPTYPES))
			migratetype = MIGRATE_MOVABLE;

		free_unref_page_commit(page, migratetype, 0);

		
		if (++batch_count == SWAP_CLUSTER_MAX) {
			local_unlock_irqrestore(&pagesets.lock, flags);
			batch_count = 0;
			local_lock_irqsave(&pagesets.lock, flags);
		}
	}
	local_unlock_irqrestore(&pagesets.lock, flags);
}

void split_page(struct page *page, unsigned int order)
{
	int i;

	VM_BUG_ON_PAGE(PageCompound(page), page);
	VM_BUG_ON_PAGE(!page_count(page), page);

	for (i = 1; i < (1 << order); i++)
		set_page_refcounted(page + i);
	split_page_owner(page, 1 << order);
	split_page_memcg(page, 1 << order);
}

int __isolate_free_page(struct page *page, unsigned int order)
{
	/* Stub: page isolation not used in minimal kernel */
	return 0;
}

void __putback_isolated_page(struct page *page, unsigned int order, int mt)
{
	/* Stub: page isolation not used in minimal kernel */
}

static inline void zone_statistics(struct zone *preferred_zone, struct zone *z,
				   long nr_account)
{
}

static inline
struct page *__rmqueue_pcplist(struct zone *zone, unsigned int order,
			int migratetype,
			unsigned int alloc_flags,
			struct per_cpu_pages *pcp,
			struct list_head *list)
{
	struct page *page;

	do {
		if (list_empty(list)) {
			int batch = READ_ONCE(pcp->batch);
			int alloced;

			
			if (batch > 1)
				batch = max(batch >> order, 2);
			alloced = rmqueue_bulk(zone, order,
					batch, list,
					migratetype, alloc_flags);

			pcp->count += alloced << order;
			if (unlikely(list_empty(list)))
				return NULL;
		}

		page = list_first_entry(list, struct page, lru);
		list_del(&page->lru);
		pcp->count -= 1 << order;
	} while (check_new_pcp(page, order));

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
	list = &pcp->lists[order_to_pindex(migratetype, order)];
	page = __rmqueue_pcplist(zone, order, migratetype, alloc_flags, pcp, list);
	local_unlock_irqrestore(&pagesets.lock, flags);
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
		
		if (!IS_ENABLED(CONFIG_CMA) || alloc_flags & ALLOC_CMA ||
				migratetype != MIGRATE_MOVABLE) {
			page = rmqueue_pcplist(preferred_zone, zone, order,
					gfp_flags, migratetype, alloc_flags);
			goto out;
		}
	}

	
	WARN_ON_ONCE((gfp_flags & __GFP_NOFAIL) && (order > 1));

	do {
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
	} while (check_new_pages(page, order));

	__count_zid_vm_events(PGALLOC, page_zonenum(page), 1 << order);
	zone_statistics(preferred_zone, zone, 1);

out:
	
	if (test_bit(ZONE_BOOSTED_WATERMARK, &zone->flags)) {
		clear_bit(ZONE_BOOSTED_WATERMARK, &zone->flags);
		wakeup_kswapd(zone, 0, 0, zone_idx(zone));
	}

	VM_BUG_ON_PAGE(page && bad_range(zone, page), page);
	return page;

failed:
	spin_unlock_irqrestore(&zone->lock, flags);
	return NULL;
}

static inline bool __should_fail_alloc_page(gfp_t gfp_mask, unsigned int order)
{
	return false;
}

noinline bool should_fail_alloc_page(gfp_t gfp_mask, unsigned int order)
{
	return __should_fail_alloc_page(gfp_mask, order);
}
ALLOW_ERROR_INJECTION(should_fail_alloc_page, TRUE);

static inline long __zone_watermark_unusable_free(struct zone *z,
				unsigned int order, unsigned int alloc_flags)
{
	const bool alloc_harder = (alloc_flags & (ALLOC_HARDER|ALLOC_OOM));
	long unusable_free = (1 << order) - 1;

	
	if (likely(!alloc_harder))
		unusable_free += z->nr_reserved_highatomic;

	return unusable_free;
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
	if (alloc_flags & (ALLOC_HARDER|ALLOC_OOM))
		min -= min / 2;

	/* Basic free pages check */
	return free_pages > min + z->lowmem_reserve[highest_zoneidx];
}

bool zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
		      int highest_zoneidx, unsigned int alloc_flags)
{
	return __zone_watermark_ok(z, order, mark, highest_zoneidx, alloc_flags,
					zone_page_state(z, NR_FREE_PAGES));
}

static inline bool zone_watermark_fast(struct zone *z, unsigned int order,
				unsigned long mark, int highest_zoneidx,
				unsigned int alloc_flags, gfp_t gfp_mask)
{
	/* Simplified fast watermark check for minimal kernel */
	long free_pages = zone_page_state(z, NR_FREE_PAGES);
	return __zone_watermark_ok(z, order, mark, highest_zoneidx, alloc_flags, free_pages);
}

bool zone_watermark_ok_safe(struct zone *z, unsigned int order,
			unsigned long mark, int highest_zoneidx)
{
	/* Stub: safe watermark check not used in minimal kernel */
	return true;
}


static inline unsigned int
alloc_flags_nofragment(struct zone *zone, gfp_t gfp_mask)
{
	unsigned int alloc_flags;

	
	alloc_flags = (__force int) (gfp_mask & __GFP_KSWAPD_RECLAIM);

	return alloc_flags;
}

static inline unsigned int gfp_to_alloc_flags_cma(gfp_t gfp_mask,
						  unsigned int alloc_flags)
{
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


void warn_alloc(gfp_t gfp_mask, nodemask_t *nodemask, const char *fmt, ...)
{
	/* Stub: allocation warning not needed for minimal kernel */
}

static inline struct page *
__alloc_pages_cpuset_fallback(gfp_t gfp_mask, unsigned int order,
			      unsigned int alloc_flags,
			      const struct alloc_context *ac)
{
	struct page *page;

	page = get_page_from_freelist(gfp_mask, order,
			alloc_flags|ALLOC_CPUSET, ac);
	
	if (!page)
		page = get_page_from_freelist(gfp_mask, order,
				alloc_flags, ac);

	return page;
}

static inline struct page *
__alloc_pages_may_oom(gfp_t gfp_mask, unsigned int order,
	const struct alloc_context *ac, unsigned long *did_some_progress)
{
	/* Stub: OOM handling not needed for minimal kernel */
	*did_some_progress = 0;
	return NULL;
}

#define MAX_COMPACT_RETRIES 16

static inline struct page *
__alloc_pages_direct_compact(gfp_t gfp_mask, unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		enum compact_priority prio, enum compact_result *compact_result)
{
	*compact_result = COMPACT_SKIPPED;
	return NULL;
}

static inline bool
should_compact_retry(struct alloc_context *ac, unsigned int order, int alloc_flags,
		     enum compact_result compact_result,
		     enum compact_priority *compact_priority,
		     int *compaction_retries)
{
	/* Stub: no compaction retry */
	return false;
}

static unsigned long
__perform_reclaim(gfp_t gfp_mask, unsigned int order,
					const struct alloc_context *ac)
{
	/* Stub: minimal reclaim attempt */
	return try_to_free_pages(ac->zonelist, order, gfp_mask, ac->nodemask);
}

static inline struct page *
__alloc_pages_direct_reclaim(gfp_t gfp_mask, unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		unsigned long *did_some_progress)
{
	/* Simplified: single reclaim attempt without retry */
	*did_some_progress = __perform_reclaim(gfp_mask, order, ac);
	if (*did_some_progress)
		return get_page_from_freelist(gfp_mask, order, alloc_flags, ac);
	return NULL;
}


static inline unsigned int
gfp_to_alloc_flags(gfp_t gfp_mask)
{
	unsigned int alloc_flags = ALLOC_WMARK_MIN | ALLOC_CPUSET;

	
	BUILD_BUG_ON(__GFP_HIGH != (__force gfp_t) ALLOC_HIGH);
	BUILD_BUG_ON(__GFP_KSWAPD_RECLAIM != (__force gfp_t) ALLOC_KSWAPD);

	
	alloc_flags |= (__force int)
		(gfp_mask & (__GFP_HIGH | __GFP_KSWAPD_RECLAIM));

	if (gfp_mask & __GFP_ATOMIC) {
		
		if (!(gfp_mask & __GFP_NOMEMALLOC))
			alloc_flags |= ALLOC_HARDER;
		
		alloc_flags &= ~ALLOC_CPUSET;
	} else if (unlikely(rt_task(current)) && in_task())
		alloc_flags |= ALLOC_HARDER;

	alloc_flags = gfp_to_alloc_flags_cma(gfp_mask, alloc_flags);

	return alloc_flags;
}

static bool oom_reserves_allowed(struct task_struct *tsk)
{
	if (!tsk_is_oom_victim(tsk))
		return false;

	
	if (!IS_ENABLED(CONFIG_MMU) && !test_thread_flag(TIF_MEMDIE))
		return false;

	return true;
}

static inline int __gfp_pfmemalloc_flags(gfp_t gfp_mask)
{
	if (unlikely(gfp_mask & __GFP_NOMEMALLOC))
		return 0;
	if (gfp_mask & __GFP_MEMALLOC)
		return ALLOC_NO_WATERMARKS;
	if (in_serving_softirq() && (current->flags & PF_MEMALLOC))
		return ALLOC_NO_WATERMARKS;
	if (!in_interrupt()) {
		if (current->flags & PF_MEMALLOC)
			return ALLOC_NO_WATERMARKS;
		else if (oom_reserves_allowed(current))
			return ALLOC_OOM;
	}

	return 0;
}

bool gfp_pfmemalloc_allowed(gfp_t gfp_mask)
{
	return !!__gfp_pfmemalloc_flags(gfp_mask);
}

static inline bool
should_reclaim_retry(gfp_t gfp_mask, unsigned order,
		     struct alloc_context *ac, int alloc_flags,
		     bool did_some_progress, int *no_progress_loops)
{
	/* Stub: minimal retry logic */
	if (did_some_progress && order <= PAGE_ALLOC_COSTLY_ORDER)
		*no_progress_loops = 0;
	else
		(*no_progress_loops)++;

	return *no_progress_loops <= MAX_RECLAIM_RETRIES;
}

static inline bool
check_retry_cpuset(int cpuset_mems_cookie, struct alloc_context *ac)
{
	
	if (cpusets_enabled() && ac->nodemask &&
			!cpuset_nodemask_valid_mems_allowed(ac->nodemask)) {
		ac->nodemask = NULL;
		return true;
	}

	
	if (read_mems_allowed_retry(cpuset_mems_cookie))
		return true;

	return false;
}

static inline struct page *
__alloc_pages_slowpath(gfp_t gfp_mask, unsigned int order,
						struct alloc_context *ac)
{
	/* Minimal stub: skip complex OOM/reclaim/compaction logic */
	struct page *page;
	unsigned int alloc_flags = gfp_to_alloc_flags(gfp_mask);

	/* Try basic allocation once */
	ac->preferred_zoneref = first_zones_zonelist(ac->zonelist,
					ac->highest_zoneidx, ac->nodemask);
	if (!ac->preferred_zoneref->zone)
		return NULL;

	page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);
	return page;
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

	if (cpusets_enabled()) {
		*alloc_gfp |= __GFP_HARDWALL;
		
		if (in_task() && !ac->nodemask)
			ac->nodemask = &cpuset_current_mems_allowed;
		else
			*alloc_flags |= ALLOC_CPUSET;
	}

	fs_reclaim_acquire(gfp_mask);
	fs_reclaim_release(gfp_mask);

	might_sleep_if(gfp_mask & __GFP_DIRECT_RECLAIM);

	if (should_fail_alloc_page(gfp_mask, order))
		return false;

	*alloc_flags = gfp_to_alloc_flags_cma(gfp_mask, *alloc_flags);

	
	ac->spread_dirty_pages = (gfp_mask & __GFP_WRITE);

	
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
	if (likely(page))
		goto out;

	alloc_gfp = gfp;
	ac.spread_dirty_pages = false;

	
	ac.nodemask = nodemask;

	page = __alloc_pages_slowpath(alloc_gfp, order, &ac);

out:
	if (memcg_kmem_enabled() && (gfp & __GFP_ACCOUNT) && page &&
	    unlikely(__memcg_kmem_charge_page(page, gfp, order) != 0)) {
		__free_pages(page, order);
		page = NULL;
	}


	return page;
}

struct folio *__folio_alloc(gfp_t gfp, unsigned int order, int preferred_nid,
		nodemask_t *nodemask)
{
	struct page *page = __alloc_pages(gfp | __GFP_COMP, order,
			preferred_nid, nodemask);

	if (page && order > 1)
		prep_transhuge_page(page);
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

unsigned long get_zeroed_page(gfp_t gfp_mask)
{
	return __get_free_pages(gfp_mask | __GFP_ZERO, 0);
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



void __page_frag_cache_drain(struct page *page, unsigned int count)
{
}

void *page_frag_alloc_align(struct page_frag_cache *nc,
		      unsigned int fragsz, gfp_t gfp_mask,
		      unsigned int align_mask)
{
	/* Stub: page fragment allocation not needed for minimal kernel */
	return NULL;
}

void page_frag_free(void *addr)
{
}

static void *make_alloc_exact(unsigned long addr, unsigned int order,
		size_t size)
{
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

void *alloc_pages_exact(size_t size, gfp_t gfp_mask)
{
	unsigned int order = get_order(size);
	unsigned long addr;

	if (WARN_ON_ONCE(gfp_mask & (__GFP_COMP | __GFP_HIGHMEM)))
		gfp_mask &= ~(__GFP_COMP | __GFP_HIGHMEM);

	addr = __get_free_pages(gfp_mask, order);
	return make_alloc_exact(addr, order, size);
}

void * __meminit alloc_pages_exact_nid(int nid, size_t size, gfp_t gfp_mask)
{
	unsigned int order = get_order(size);
	struct page *p;

	if (WARN_ON_ONCE(gfp_mask & (__GFP_COMP | __GFP_HIGHMEM)))
		gfp_mask &= ~(__GFP_COMP | __GFP_HIGHMEM);

	p = alloc_pages_node(nid, gfp_mask, order);
	if (!p)
		return NULL;
	return make_alloc_exact((unsigned long)page_address(p), order, size);
}

void free_pages_exact(void *virt, size_t size)
{
	/* Stub: exact page freeing not used in minimal kernel */
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

unsigned long nr_free_buffer_pages(void)
{
	return nr_free_zone_pages(gfp_zone(GFP_USER));
}

static inline void show_node(struct zone *zone)
{
	if (IS_ENABLED(CONFIG_NUMA))
		printk("Node %d ", zone_to_nid(zone));
}

long si_mem_available(void)
{
	/* Stub: memory info reporting not needed for minimal kernel */
	return global_zone_page_state(NR_FREE_PAGES);
}

void si_meminfo(struct sysinfo *val)
{
	/* Stub: sysinfo not needed for minimal kernel */
}


#define K(x) ((x) << (PAGE_SHIFT-10))

void show_free_areas(unsigned int filter, nodemask_t *nodemask)
{
	
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
			check_highest_zone(zone_type);
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

static void per_cpu_pages_init(struct per_cpu_pages *pcp, struct per_cpu_zonestat *pzstats);

#define BOOT_PAGESET_HIGH	0
#define BOOT_PAGESET_BATCH	1
static DEFINE_PER_CPU(struct per_cpu_pages, boot_pageset);
static DEFINE_PER_CPU(struct per_cpu_zonestat, boot_zonestats);
DEFINE_PER_CPU(struct per_cpu_nodestat, boot_nodestats);

static void __build_all_zonelists(void *data)
{
	int nid;
	int __maybe_unused cpu;
	pg_data_t *self = data;
	static DEFINE_SPINLOCK(lock);

	spin_lock(&lock);

	
	if (self && !node_online(self->node_id)) {
		build_zonelists(self);
	} else {
		
		for_each_node(nid) {
			pg_data_t *pgdat = NODE_DATA(nid);

			build_zonelists(pgdat);
		}

	}

	spin_unlock(&lock);
}

static noinline void __init
build_all_zonelists_init(void)
{
	int cpu;

	__build_all_zonelists(NULL);

	
	for_each_possible_cpu(cpu)
		per_cpu_pages_init(&per_cpu(boot_pageset, cpu), &per_cpu(boot_zonestats, cpu));

	mminit_verify_zonelist();
	cpuset_init_current_mems_allowed();
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

static bool __meminit
overlap_memmap_init(unsigned long zone, unsigned long *pfn)
{
	/* Stub: no mirrored kernel core support */
	return false;
}

void __meminit memmap_init_range(unsigned long size, int nid, unsigned long zone,
		unsigned long start_pfn, unsigned long zone_end_pfn,
		enum meminit_context context,
		struct vmem_altmap *altmap, int migratetype)
{
	unsigned long pfn, end_pfn = start_pfn + size;
	struct page *page;

	if (highest_memmap_pfn < end_pfn - 1)
		highest_memmap_pfn = end_pfn - 1;

	for (pfn = start_pfn; pfn < end_pfn; ) {
		
		if (context == MEMINIT_EARLY) {
			if (overlap_memmap_init(zone, &pfn))
				continue;
			if (defer_init(nid, pfn, zone_end_pfn))
				break;
		}

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

void __init *memmap_alloc(phys_addr_t size, phys_addr_t align,
			  phys_addr_t min_addr, int nid, bool exact_nid)
{
	void *ptr;

	if (exact_nid)
		ptr = memblock_alloc_exact_nid_raw(size, align, min_addr,
						   MEMBLOCK_ALLOC_ACCESSIBLE,
						   nid);
	else
		ptr = memblock_alloc_try_nid_raw(size, align, min_addr,
						 MEMBLOCK_ALLOC_ACCESSIBLE,
						 nid);

	if (ptr && size > 0)
		page_init_poison(ptr, size);

	return ptr;
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
	WRITE_ONCE(pcp->high, high);
}

static void per_cpu_pages_init(struct per_cpu_pages *pcp, struct per_cpu_zonestat *pzstats)
{
	int pindex;

	memset(pcp, 0, sizeof(*pcp));
	memset(pzstats, 0, sizeof(*pzstats));

	for (pindex = 0; pindex < NR_PCP_LISTS; pindex++)
		INIT_LIST_HEAD(&pcp->lists[pindex]);

	
	pcp->high = BOOT_PAGESET_HIGH;
	pcp->batch = BOOT_PAGESET_BATCH;
	pcp->free_factor = 0;
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

	
	if (sizeof(struct per_cpu_zonestat) > 0)
		zone->per_cpu_zonestats = alloc_percpu(struct per_cpu_zonestat);

	zone->per_cpu_pageset = alloc_percpu(struct per_cpu_pages);
	for_each_possible_cpu(cpu) {
		struct per_cpu_pages *pcp;
		struct per_cpu_zonestat *pzstats;

		pcp = per_cpu_ptr(zone->per_cpu_pageset, cpu);
		pzstats = per_cpu_ptr(zone->per_cpu_zonestats, cpu);
		per_cpu_pages_init(pcp, pzstats);
	}

	zone_set_pageset_high_and_batch(zone, 0);
}

void __init setup_per_cpu_pageset(void)
{
	struct pglist_data *pgdat;
	struct zone *zone;
	int __maybe_unused cpu;

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

	mminit_dprintk(MMINIT_TRACE, "memmap_init",
			"Initialising map node %d zone %lu pfns %lu -> %lu\n",
			pgdat->node_id,
			(unsigned long)zone_idx(zone),
			zone_start_pfn, (zone_start_pfn + size));

	zone_init_free_lists(zone);
	zone->initialized = 1;
}

void __init get_pfn_range_for_nid(unsigned int nid,
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

static void __init adjust_zone_range_for_zone_movable(int nid,
					unsigned long zone_type,
					unsigned long node_start_pfn,
					unsigned long node_end_pfn,
					unsigned long *zone_start_pfn,
					unsigned long *zone_end_pfn)
{
	
	if (zone_movable_pfn[nid]) {
		
		if (zone_type == ZONE_MOVABLE) {
			*zone_start_pfn = zone_movable_pfn[nid];
			*zone_end_pfn = min(node_end_pfn,
				arch_zone_highest_possible_pfn[movable_zone]);

		
		} else if (!mirrored_kernelcore &&
			*zone_start_pfn < zone_movable_pfn[nid] &&
			*zone_end_pfn > zone_movable_pfn[nid]) {
			*zone_end_pfn = zone_movable_pfn[nid];

		
		} else if (*zone_start_pfn >= zone_movable_pfn[nid])
			*zone_start_pfn = *zone_end_pfn;
	}
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
	adjust_zone_range_for_zone_movable(nid, zone_type,
				node_start_pfn, node_end_pfn,
				zone_start_pfn, zone_end_pfn);

	
	if (*zone_end_pfn < node_start_pfn || *zone_start_pfn > node_end_pfn)
		return 0;

	
	*zone_end_pfn = min(*zone_end_pfn, node_end_pfn);
	*zone_start_pfn = max(*zone_start_pfn, node_start_pfn);

	
	return *zone_end_pfn - *zone_start_pfn;
}

unsigned long __init __absent_pages_in_range(int nid,
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

unsigned long __init absent_pages_in_range(unsigned long start_pfn,
							unsigned long end_pfn)
{
	return __absent_pages_in_range(MAX_NUMNODES, start_pfn, end_pfn);
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

	adjust_zone_range_for_zone_movable(nid, zone_type,
			node_start_pfn, node_end_pfn,
			&zone_start_pfn, &zone_end_pfn);
	nr_absent = __absent_pages_in_range(nid, zone_start_pfn, zone_end_pfn);

	/* Stub: mirrored_kernelcore logic not needed for minimal kernel */

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
		realtotalpages += real_size;
	}

	pgdat->node_spanned_pages = totalpages;
	pgdat->node_present_pages = realtotalpages;
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

void __init set_pageblock_order(void)
{
}

static unsigned long __init calc_memmap_size(unsigned long spanned_pages,
						unsigned long present_pages)
{
	unsigned long pages = spanned_pages;

	
	if (spanned_pages > present_pages + (present_pages >> 4) &&
	    IS_ENABLED(CONFIG_SPARSEMEM))
		pages = present_pages;

	return PAGE_ALIGN(pages * sizeof(struct page)) >> PAGE_SHIFT;
}

static void pgdat_init_split_queue(struct pglist_data *pgdat) {}

static void pgdat_init_kcompactd(struct pglist_data *pgdat) {}

static void __meminit pgdat_init_internals(struct pglist_data *pgdat)
{
	int i;

	pgdat_resize_init(pgdat);

	pgdat_init_split_queue(pgdat);
	pgdat_init_kcompactd(pgdat);

	init_waitqueue_head(&pgdat->kswapd_wait);
	init_waitqueue_head(&pgdat->pfmemalloc_wait);

	for (i = 0; i < NR_VMSCAN_THROTTLE; i++)
		init_waitqueue_head(&pgdat->reclaim_wait[i]);

	pgdat_page_ext_init(pgdat);
	lruvec_init(&pgdat->__lruvec);
}

static void __meminit zone_init_internals(struct zone *zone, enum zone_type idx, int nid,
							unsigned long remaining_pages)
{
	atomic_long_set(&zone->managed_pages, remaining_pages);
	zone_set_nid(zone, nid);
	zone->name = zone_names[idx];
	zone->zone_pgdat = NODE_DATA(nid);
	spin_lock_init(&zone->lock);
	zone_seqlock_init(zone);
	zone_pcp_init(zone);
}

static void __init free_area_init_core(struct pglist_data *pgdat)
{
	enum zone_type j;
	int nid = pgdat->node_id;

	pgdat_init_internals(pgdat);
	pgdat->per_cpu_nodestats = &boot_nodestats;

	for (j = 0; j < MAX_NR_ZONES; j++) {
		struct zone *zone = pgdat->node_zones + j;
		unsigned long size, freesize, memmap_pages;

		size = zone->spanned_pages;
		freesize = zone->present_pages;

		
		memmap_pages = calc_memmap_size(size, freesize);
		if (!is_highmem_idx(j)) {
			if (freesize >= memmap_pages) {
				freesize -= memmap_pages;
			}
		}

		
		if (j == 0 && freesize > dma_reserve) {
			freesize -= dma_reserve;
		}

		if (!is_highmem_idx(j))
			nr_kernel_pages += freesize;
		
		else if (nr_kernel_pages > memmap_pages * 2)
			nr_kernel_pages -= memmap_pages;
		nr_all_pages += freesize;

		
		zone_init_internals(zone, j, nid, freesize);

		if (!size)
			continue;

		set_pageblock_order();
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

static inline void pgdat_set_deferred_range(pg_data_t *pgdat) {}

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
	pgdat_set_deferred_range(pgdat);

	free_area_init_core(pgdat);
}


#if MAX_NUMNODES > 1

void __init setup_nr_node_ids(void)
{
	unsigned int highest;

	highest = find_last_bit(node_possible_map.bits, MAX_NUMNODES);
	nr_node_ids = highest + 1;
}
#endif

unsigned long __init node_map_pfn_alignment(void)
{
	/* Stub: minimal single-node system doesn't need alignment calculation */
	return PAGE_SIZE;
}

unsigned long __init find_min_pfn_with_active_regions(void)
{
	return PHYS_PFN(memblock_start_of_DRAM());
}




bool __weak arch_has_descending_max_zone_pfns(void)
{
	return false;
}

void __init free_area_init(unsigned long *max_zone_pfn)
{
	unsigned long start_pfn, end_pfn;
	int i, nid;

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

	/* Basic node initialization */
	setup_nr_node_ids();
	for_each_node(nid) {
		if (node_online(nid)) {
			free_area_init_node(nid);
		}
	}

	memmap_init();
}

static int __init cmdline_parse_kernelcore(char *p)
{
	/* Stub: kernelcore parameter not needed for minimal system */
	return 0;
}

static int __init cmdline_parse_movablecore(char *p)
{
	/* Stub: movablecore parameter not needed for minimal system */
	return 0;
}

early_param("kernelcore", cmdline_parse_kernelcore);
early_param("movablecore", cmdline_parse_movablecore);

void adjust_managed_page_count(struct page *page, long count)
{
	atomic_long_add(count, &page_zone(page)->managed_pages);
	totalram_pages_add(count);
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

void __init set_dma_reserve(unsigned long new_dma_reserve)
{
	dma_reserve = new_dma_reserve;
}

static int page_alloc_cpu_dead(unsigned int cpu)
{
	struct zone *zone;

	lru_add_drain_cpu(cpu);
	mlock_page_drain_remote(cpu);
	drain_pages(cpu);

	
	vm_events_fold_cpu(cpu);

	
	cpu_vm_stats_fold(cpu);

	for_each_populated_zone(zone)
		zone_pcp_update(zone, 0);

	return 0;
}

static int page_alloc_cpu_online(unsigned int cpu)
{
	struct zone *zone;

	for_each_populated_zone(zone)
		zone_pcp_update(zone, 1);
	return 0;
}

void __init page_alloc_init(void)
{
	int ret;

	ret = cpuhp_setup_state_nocalls(CPUHP_PAGE_ALLOC,
					"mm/page_alloc:pcp",
					page_alloc_cpu_online,
					page_alloc_cpu_dead);
	WARN_ON(ret < 0);
}

static void calculate_totalreserve_pages(void)
{
	/* Stub: skip complex reserve calculation for minimal system */
	totalreserve_pages = 0;
}

static void setup_per_zone_lowmem_reserve(void)
{
	/* Stub: skip lowmem reserve setup for minimal system */
	calculate_totalreserve_pages();
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
		zone->_watermark[WMARK_PROMO] = 768;
		zone->watermark_boost = 0;
		spin_unlock_irqrestore(&zone->lock, flags);
	}
	calculate_totalreserve_pages();
}

void setup_per_zone_wmarks(void)
{
	struct zone *zone;
	static DEFINE_SPINLOCK(lock);

	spin_lock(&lock);
	__setup_per_zone_wmarks();
	spin_unlock(&lock);

	
	for_each_zone(zone)
		zone_pcp_update(zone, 0);
}

void calculate_min_free_kbytes(void)
{
	unsigned long lowmem_kbytes;
	int new_min_free_kbytes;

	lowmem_kbytes = nr_free_buffer_pages() * (PAGE_SIZE >> 10);
	new_min_free_kbytes = int_sqrt(lowmem_kbytes * 16);

	if (new_min_free_kbytes > user_min_free_kbytes)
		min_free_kbytes = clamp(new_min_free_kbytes, 128, 262144);

}

int __meminit init_per_zone_wmark_min(void)
{
	calculate_min_free_kbytes();
	setup_per_zone_wmarks();
	refresh_zone_stat_thresholds();
	setup_per_zone_lowmem_reserve();

	khugepaged_min_free_kbytes_update();

	return 0;
}
postcore_initcall(init_per_zone_wmark_min)

int min_free_kbytes_sysctl_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	/* Stub: minimal sysctl handler */
	return proc_dointvec_minmax(table, write, buffer, length, ppos);
}

int watermark_scale_factor_sysctl_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	/* Stub: minimal sysctl handler */
	return proc_dointvec_minmax(table, write, buffer, length, ppos);
}

int lowmem_reserve_ratio_sysctl_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	/* Stub: minimal sysctl handler */
	proc_dointvec_minmax(table, write, buffer, length, ppos);
	return 0;
}

int percpu_pagelist_high_fraction_sysctl_handler(struct ctl_table *table,
		int write, void *buffer, size_t *length, loff_t *ppos)
{
	/* Stub: minimal sysctl handler */
	return proc_dointvec_minmax(table, write, buffer, length, ppos);
}

#ifndef __HAVE_ARCH_RESERVED_KERNEL_PAGES

#endif

#if __BITS_PER_LONG > 32
#define ADAPT_SCALE_BASE	(64ul << 30)
#define ADAPT_SCALE_SHIFT	2
#define ADAPT_SCALE_NPAGES	(ADAPT_SCALE_BASE >> PAGE_SHIFT)
#endif

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

void free_contig_range(unsigned long pfn, unsigned long nr_pages)
{
	/* Stub: contiguous allocation not needed for minimal kernel */
}

void zone_pcp_update(struct zone *zone, int cpu_online)
{
	mutex_lock(&pcp_batch_high_lock);
	zone_set_pageset_high_and_batch(zone, cpu_online);
	mutex_unlock(&pcp_batch_high_lock);
}

void zone_pcp_disable(struct zone *zone)
{
	/* Stub: PCP disable not used in minimal kernel */
}

void zone_pcp_enable(struct zone *zone)
{
	/* Stub: PCP enable not used in minimal kernel */
}

void zone_pcp_reset(struct zone *zone)
{
	/* Stub: PCP reset not needed for minimal single-CPU kernel */
}

bool is_free_buddy_page(struct page *page)
{
	/* Stub: buddy page check not needed for minimal kernel */
	return false;
}

