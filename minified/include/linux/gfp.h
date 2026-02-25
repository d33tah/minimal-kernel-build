#ifndef __LINUX_GFP_H
#define __LINUX_GFP_H

#include <linux/mmdebug.h>
#include <linux/stddef.h>
#include <linux/topology.h>

struct vm_area_struct;

#define __GFP_HIGHMEM	((__force gfp_t)0x02u)
#define __GFP_DMA32	((__force gfp_t)0x04u)
#define __GFP_MOVABLE	((__force gfp_t)0x08u)
#define GFP_ZONEMASK	(__GFP_HIGHMEM|__GFP_DMA32|__GFP_MOVABLE)
#define __GFP_RECLAIMABLE ((__force gfp_t)0x10u)
#define __GFP_HARDWALL   ((__force gfp_t)0x100000u)
#define __GFP_THISNODE	((__force gfp_t)0x200000u)
#define __GFP_ACCOUNT	((__force gfp_t)0x400000u)
#define __GFP_ATOMIC	((__force gfp_t)0x200u)
#define __GFP_HIGH	((__force gfp_t)0x20u)
#define __GFP_MEMALLOC	((__force gfp_t)0x20000u)
#define __GFP_NOMEMALLOC ((__force gfp_t)0x80000u)
#define __GFP_IO	((__force gfp_t)0x40u)
#define __GFP_FS	((__force gfp_t)0x80u)
#define __GFP_DIRECT_RECLAIM	((__force gfp_t)0x400u)
#define __GFP_KSWAPD_RECLAIM	((__force gfp_t)0x800u)
#define __GFP_RECLAIM ((__force gfp_t)(0x400u|0x800u))
#define __GFP_RETRY_MAYFAIL	((__force gfp_t)0x4000u)
#define __GFP_NOFAIL	((__force gfp_t)0x8000u)
#define __GFP_NORETRY	((__force gfp_t)0x10000u)
#define __GFP_NOWARN	((__force gfp_t)0x2000u)
#define __GFP_COMP	((__force gfp_t)0x40000u)
#define __GFP_ZERO	((__force gfp_t)0x100u)

/* CONFIG_LOCKDEP not enabled */
#define __GFP_BITS_SHIFT 27
#define __GFP_BITS_MASK ((__force gfp_t)((1 << __GFP_BITS_SHIFT) - 1))

#define GFP_ATOMIC	(__GFP_HIGH|__GFP_ATOMIC|__GFP_KSWAPD_RECLAIM)
#define GFP_KERNEL	(__GFP_RECLAIM | __GFP_IO | __GFP_FS)
#define GFP_KERNEL_ACCOUNT (GFP_KERNEL | __GFP_ACCOUNT)
#define GFP_NOWAIT	(__GFP_KSWAPD_RECLAIM)
#define GFP_USER	(__GFP_RECLAIM | __GFP_IO | __GFP_FS | __GFP_HARDWALL)
#define GFP_HIGHUSER	(GFP_USER | __GFP_HIGHMEM)
#define GFP_HIGHUSER_MOVABLE	(GFP_HIGHUSER | __GFP_MOVABLE)

static inline bool gfpflags_allow_blocking(const gfp_t gfp_flags)
{
	return !!(gfp_flags & __GFP_DIRECT_RECLAIM);
}

/* Only ZONE_NORMAL and ZONE_MOVABLE exist; no DMA/HIGHMEM/DMA32 zones.
 * MOVABLE+HIGHMEM -> ZONE_MOVABLE, everything else -> ZONE_NORMAL. */
static inline enum zone_type gfp_zone(gfp_t flags)
{
	if (((__force int)flags & (0x08u | 0x02u)) ==
	    (0x08u | 0x02u))
		return ZONE_MOVABLE;
	return ZONE_NORMAL;
}

static inline struct zonelist *node_zonelist(int nid, gfp_t flags)
{
	return NODE_DATA(nid)->node_zonelists + ZONELIST_FALLBACK;
}

struct page *__alloc_pages(gfp_t gfp, unsigned int order, int preferred_nid,
		nodemask_t *nodemask);
struct folio *__folio_alloc(gfp_t gfp, unsigned int order, int preferred_nid,
		nodemask_t *nodemask);


static inline struct page *
__alloc_pages_node(int nid, gfp_t gfp_mask, unsigned int order)
{
	VM_BUG_ON(nid < 0 || nid >= MAX_NUMNODES);
	VM_WARN_ON((gfp_mask & __GFP_THISNODE) && !node_online(nid));

	return __alloc_pages(gfp_mask, order, nid, NULL);
}

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
	return __folio_alloc(gfp, order, numa_node_id(), NULL);
}
#define vma_alloc_folio(gfp, order, vma, addr, hugepage)		\
	folio_alloc(gfp, order)
#define alloc_page(gfp_mask) alloc_pages(gfp_mask, 0)
static inline struct page *alloc_page_vma(gfp_t gfp,
		struct vm_area_struct *vma, unsigned long addr)
{
	struct folio *folio = vma_alloc_folio(gfp, 0, vma, addr, false);

	return &folio->page;
}

extern unsigned long __get_free_pages(gfp_t gfp_mask, unsigned int order);

extern gfp_t gfp_allowed_mask;

#endif
