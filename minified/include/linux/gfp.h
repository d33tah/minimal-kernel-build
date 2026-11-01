/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_GFP_H
#define __LINUX_GFP_H

#include <linux/topology.h>

struct vm_area_struct;

#define ___GFP_HIGHMEM 0x02u
#define ___GFP_DMA32 0x04u
#define ___GFP_MOVABLE 0x08u
#define ___GFP_RECLAIMABLE 0x10u
#define ___GFP_HIGH 0x20u
#define ___GFP_IO 0x40u
#define ___GFP_FS 0x80u
#define ___GFP_ZERO 0x100u
#define ___GFP_ATOMIC 0x200u
#define ___GFP_DIRECT_RECLAIM 0x400u
#define ___GFP_KSWAPD_RECLAIM 0x800u
#define ___GFP_NOWARN 0x2000u
#define ___GFP_RETRY_MAYFAIL 0x4000u
#define ___GFP_NOFAIL 0x8000u
#define ___GFP_NORETRY 0x10000u
#define ___GFP_MEMALLOC 0x20000u
#define ___GFP_COMP 0x40000u
#define ___GFP_NOMEMALLOC 0x80000u
#define ___GFP_HARDWALL 0x100000u
#define ___GFP_THISNODE 0x200000u
#define ___GFP_ACCOUNT 0x400000u
#define ___GFP_SKIP_KASAN_POISON 0
#define ___GFP_NOLOCKDEP 0

#define __GFP_HIGHMEM ((__force gfp_t)___GFP_HIGHMEM)
#define __GFP_DMA32 ((__force gfp_t)___GFP_DMA32)
#define __GFP_MOVABLE ((__force gfp_t)___GFP_MOVABLE) /* ZONE_MOVABLE allowed */

/**
 * DOC: Page mobility and placement hints
 *
 * Page mobility and placement hints
 * ---------------------------------
 *
 * These flags provide hints about how mobile the page is. Pages with similar
 * mobility are placed within the same pageblocks to minimise problems due
 * to external fragmentation.
 *
 * %__GFP_MOVABLE (also a zone modifier) indicates that the page can be
 * moved by page migration during memory compaction or can be reclaimed.
 *
 * %__GFP_RECLAIMABLE is used for slab allocations that specify
 * SLAB_RECLAIM_ACCOUNT and whose pages can be freed via shrinkers.
 *
 * %__GFP_WRITE indicates the caller intends to dirty the page. Where possible,
 * these pages will be spread between local zones to avoid all the dirty
 * pages being in one zone (fair zone allocation policy).
 *
 * %__GFP_HARDWALL enforces the cpuset memory allocation policy.
 *
 * %__GFP_THISNODE forces the allocation to be satisfied from the requested
 * node with no fallbacks or placement policy enforcements.
 *
 * %__GFP_ACCOUNT causes the allocation to be accounted to kmemcg.
 */
#define __GFP_RECLAIMABLE ((__force gfp_t)___GFP_RECLAIMABLE)
#define __GFP_HARDWALL ((__force gfp_t)___GFP_HARDWALL)
#define __GFP_THISNODE ((__force gfp_t)___GFP_THISNODE)
#define __GFP_ACCOUNT ((__force gfp_t)___GFP_ACCOUNT)

/**
 * DOC: Watermark modifiers
 *
 * Watermark modifiers -- controls access to emergency reserves
 * ------------------------------------------------------------
 *
 * %__GFP_HIGH indicates that the caller is high-priority and that granting
 * the request is necessary before the system can make forward progress.
 * For example, creating an IO context to clean pages.
 *
 * %__GFP_ATOMIC indicates that the caller cannot reclaim or sleep and is
 * high priority. Users are typically interrupt handlers. This may be
 * used in conjunction with %__GFP_HIGH
 *
 * %__GFP_MEMALLOC allows access to all memory. This should only be used when
 * the caller guarantees the allocation will allow more memory to be freed
 * very shortly e.g. process exiting or swapping. Users either should
 * be the MM or co-ordinating closely with the VM (e.g. swap over NFS).
 * Users of this flag have to be extremely careful to not deplete the reserve
 * completely and implement a throttling mechanism which controls the
 * consumption of the reserve based on the amount of freed memory.
 * Usage of a pre-allocated pool (e.g. mempool) should be always considered
 * before using this flag.
 *
 * %__GFP_NOMEMALLOC is used to explicitly forbid access to emergency reserves.
 * This takes precedence over the %__GFP_MEMALLOC flag if both are set.
 */
#define __GFP_ATOMIC ((__force gfp_t)___GFP_ATOMIC)
#define __GFP_HIGH ((__force gfp_t)___GFP_HIGH)
#define __GFP_MEMALLOC ((__force gfp_t)___GFP_MEMALLOC)
#define __GFP_NOMEMALLOC ((__force gfp_t)___GFP_NOMEMALLOC)

/**
 * DOC: Reclaim modifiers
 *
 * Reclaim modifiers
 * -----------------
 * Please note that all the following flags are only applicable to sleepable
 * allocations (e.g. %GFP_NOWAIT and %GFP_ATOMIC will ignore them).
 *
 * %__GFP_IO can start physical IO.
 *
 * %__GFP_FS can call down to the low-level FS. Clearing the flag avoids the
 * allocator recursing into the filesystem which might already be holding
 * locks.
 *
 * %__GFP_DIRECT_RECLAIM indicates that the caller may enter direct reclaim.
 * This flag can be cleared to avoid unnecessary delays when a fallback
 * option is available.
 *
 * %__GFP_KSWAPD_RECLAIM indicates that the caller wants to wake kswapd when
 * the low watermark is reached and have it reclaim pages until the high
 * watermark is reached. A caller may wish to clear this flag when fallback
 * options are available and the reclaim is likely to disrupt the system. The
 * canonical example is THP allocation where a fallback is cheap but
 * reclaim/compaction may cause indirect stalls.
 *
 * %__GFP_RECLAIM is shorthand to allow/forbid both direct and kswapd reclaim.
 *
 * The default allocator behavior depends on the request size. We have a concept
 * of so called costly allocations (with order > %PAGE_ALLOC_COSTLY_ORDER).
 * !costly allocations are too essential to fail so they are implicitly
 * non-failing by default (with some exceptions like OOM victims might fail so
 * the caller still has to check for failures) while costly requests try to be
 * not disruptive and back off even without invoking the OOM killer.
 * The following three modifiers might be used to override some of these
 * implicit rules
 *
 * %__GFP_NORETRY: The VM implementation will try only very lightweight
 * memory direct reclaim to get some memory under memory pressure (thus
 * it can sleep). It will avoid disruptive actions like OOM killer. The
 * caller must handle the failure which is quite likely to happen under
 * heavy memory pressure. The flag is suitable when failure can easily be
 * handled at small cost, such as reduced throughput
 *
 * %__GFP_RETRY_MAYFAIL: The VM implementation will retry memory reclaim
 * procedures that have previously failed if there is some indication
 * that progress has been made else where.  It can wait for other
 * tasks to attempt high level approaches to freeing memory such as
 * compaction (which removes fragmentation) and page-out.
 * There is still a definite limit to the number of retries, but it is
 * a larger limit than with %__GFP_NORETRY.
 * Allocations with this flag may fail, but only when there is
 * genuinely little unused memory. While these allocations do not
 * directly trigger the OOM killer, their failure indicates that
 * the system is likely to need to use the OOM killer soon.  The
 * caller must handle failure, but can reasonably do so by failing
 * a higher-level request, or completing it only in a much less
 * efficient manner.
 * If the allocation does fail, and the caller is in a position to
 * free some non-essential memory, doing so could benefit the system
 * as a whole.
 *
 * %__GFP_NOFAIL: The VM implementation _must_ retry infinitely: the caller
 * cannot handle allocation failures. The allocation could block
 * indefinitely but will never return with failure. Testing for
 * failure is pointless.
 * New users should be evaluated carefully (and the flag should be
 * used only when there is no reasonable failure policy) but it is
 * definitely preferable to use the flag rather than opencode endless
 * loop around allocator.
 * Using this flag for costly allocations is _highly_ discouraged.
 */
#define __GFP_IO ((__force gfp_t)___GFP_IO)
#define __GFP_FS ((__force gfp_t)___GFP_FS)
#define __GFP_DIRECT_RECLAIM ((__force gfp_t)___GFP_DIRECT_RECLAIM)
#define __GFP_KSWAPD_RECLAIM ((__force gfp_t)___GFP_KSWAPD_RECLAIM)
#define __GFP_RECLAIM \
	((__force gfp_t)(___GFP_DIRECT_RECLAIM | ___GFP_KSWAPD_RECLAIM))
#define __GFP_RETRY_MAYFAIL ((__force gfp_t)___GFP_RETRY_MAYFAIL)
#define __GFP_NOFAIL ((__force gfp_t)___GFP_NOFAIL)
#define __GFP_NORETRY ((__force gfp_t)___GFP_NORETRY)

/**
 * DOC: Action modifiers
 *
 * Action modifiers
 * ----------------
 *
 * %__GFP_NOWARN suppresses allocation failure reports.
 *
 * %__GFP_COMP address compound page metadata.
 *
 * %__GFP_ZERO returns a zeroed page on success.
 *
 * %__GFP_ZEROTAGS zeroes memory tags at allocation time if the memory itself
 * is being zeroed (either via __GFP_ZERO or via init_on_alloc, provided that
 * __GFP_SKIP_ZERO is not set). This flag is intended for optimization: setting
 * memory tags at the same time as zeroing memory has minimal additional
 * performace impact.
 *
 * %__GFP_SKIP_KASAN_UNPOISON makes KASAN skip unpoisoning on page allocation.
 * Only effective in HW_TAGS mode.
 *
 * %__GFP_SKIP_KASAN_POISON makes KASAN skip poisoning on page deallocation.
 * Typically, used for userspace pages. Only effective in HW_TAGS mode.
 */
#define __GFP_NOWARN ((__force gfp_t)___GFP_NOWARN)
#define __GFP_COMP ((__force gfp_t)___GFP_COMP)
#define __GFP_ZERO ((__force gfp_t)___GFP_ZERO)
#define __GFP_SKIP_KASAN_POISON ((__force gfp_t)___GFP_SKIP_KASAN_POISON)

/* Disable lockdep for GFP context tracking */
#define __GFP_NOLOCKDEP ((__force gfp_t)___GFP_NOLOCKDEP)

/* Room for N __GFP_FOO bits */
#define __GFP_BITS_SHIFT (27 + IS_ENABLED(CONFIG_LOCKDEP))
#define __GFP_BITS_MASK ((__force gfp_t)((1 << __GFP_BITS_SHIFT) - 1))

#define GFP_KERNEL (__GFP_RECLAIM | __GFP_IO | __GFP_FS)
#define GFP_KERNEL_ACCOUNT (GFP_KERNEL | __GFP_ACCOUNT)
#define GFP_NOWAIT (__GFP_KSWAPD_RECLAIM)
#define GFP_USER (__GFP_RECLAIM | __GFP_IO | __GFP_FS | __GFP_HARDWALL)
#define GFP_HIGHUSER (GFP_USER | __GFP_HIGHMEM)
#define GFP_HIGHUSER_MOVABLE \
	(GFP_HIGHUSER | __GFP_MOVABLE | __GFP_SKIP_KASAN_POISON)

struct page *__alloc_pages(gfp_t gfp, unsigned int order, int preferred_nid);
struct folio *__folio_alloc(gfp_t gfp, unsigned int order, int preferred_nid);

static inline struct page *__alloc_pages_node(int nid, gfp_t gfp_mask,
					      unsigned int order)
{
	return __alloc_pages(gfp_mask, order, nid);
}

static inline struct folio *__folio_alloc_node(gfp_t gfp, unsigned int order,
					       int nid)
{
	return __folio_alloc(gfp, order, nid);
}

/*
 * Allocate pages, preferring the node given as nid. When nid == NUMA_NO_NODE,
 * prefer the current CPU's closest node. Otherwise node must be valid and
 * online.
 */
static inline struct page *alloc_pages_node(int nid, gfp_t gfp_mask,
					    unsigned int order)
{
	if (nid == NUMA_NO_NODE)
		nid = numa_mem_id();

	return __alloc_pages_node(nid, gfp_mask, order);
}

static inline struct page *alloc_pages(gfp_t gfp_mask, unsigned int order)
{
	return alloc_pages_node(numa_node_id(), gfp_mask, order);
}
static inline struct folio *folio_alloc(gfp_t gfp, unsigned int order)
{
	return __folio_alloc_node(gfp, order, numa_node_id());
}
#define vma_alloc_folio(gfp, order, vma, addr, hugepage) folio_alloc(gfp, order)
#define alloc_page(gfp_mask) alloc_pages(gfp_mask, 0)
static inline struct page *alloc_page_vma(gfp_t gfp, struct vm_area_struct *vma,
					  unsigned long addr)
{
	struct folio *folio = vma_alloc_folio(gfp, 0, vma, addr, false);

	return &folio->page;
}

extern unsigned long __get_free_pages(gfp_t gfp_mask, unsigned int order);

extern void __free_pages(struct page *page, unsigned int order);
extern void free_pages(unsigned long addr, unsigned int order);

#define __free_page(page) __free_pages((page), 0)

/*
 * gfp_allowed_mask is set to GFP_BOOT_MASK during early boot to restrict what
 * GFP flags are used before interrupts are enabled. Once interrupts are
 * enabled, it is set to __GFP_BITS_MASK while the system is running. During
 * hibernation, it is used by PM to avoid I/O during memory allocation while
 * devices are suspended.
 */
extern gfp_t gfp_allowed_mask;

#endif
