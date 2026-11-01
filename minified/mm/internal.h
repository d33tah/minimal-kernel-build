#ifndef __MM_INTERNAL_H
#define __MM_INTERNAL_H

#include <linux/rmap.h>

#define GFP_RECLAIM_MASK                                                       \
	(__GFP_RECLAIM | __GFP_HIGH | __GFP_IO | __GFP_FS | __GFP_NOWARN |     \
	 __GFP_RETRY_MAYFAIL | __GFP_NOFAIL | __GFP_NORETRY | __GFP_MEMALLOC | \
	 __GFP_NOMEMALLOC | __GFP_ATOMIC | __GFP_NOLOCKDEP)

#define GFP_BOOT_MASK (__GFP_BITS_MASK & ~(__GFP_RECLAIM | __GFP_IO | __GFP_FS))

#define GFP_CONSTRAINT_MASK (__GFP_HARDWALL | __GFP_THISNODE)

#define GFP_SLAB_BUG_MASK (__GFP_DMA32 | __GFP_HIGHMEM | ~__GFP_BITS_MASK)

#define WARN_ON_ONCE_GFP(cond, gfp)             \
	({                                      \
		int __ret_warn_once = !!(cond); \
		unlikely(__ret_warn_once);      \
	})

static inline void set_page_refcounted(struct page *page)
{
	set_page_count(page, 1);
}

extern unsigned long highest_memmap_pfn;

/*
 * This function returns the order of a free page in the buddy system. In
 * general, page_zone(page)->lock must be held by the caller to prevent the
 * page from being allocated in parallel and returning garbage as the order.
 * If a caller does not hold page_zone(page)->lock, it must guarantee that the
 * page cannot be allocated or merged in parallel.
 */
static inline unsigned int buddy_order(struct page *page)
{
	/* PageBuddy() must be checked by the caller */
	return page_private(page);
}

/*
 * This function checks whether a page is free && is the buddy
 * we can coalesce a page and its buddy if
 * (a) the buddy is not in a hole (check before calling!) &&
 * (b) the buddy is in the buddy system &&
 * (c) a page and its buddy have the same order &&
 * (d) a page and its buddy are in the same zone.
 *
 * For recording whether a page is in the buddy system, we set PageBuddy.
 * Setting, clearing, and testing PageBuddy is serialized by zone->lock.
 *
 * For recording page's order, we use page_private(page).
 */
static inline bool page_is_buddy(struct page *page, struct page *buddy,
				 unsigned int order)
{
	if (!PageBuddy(buddy))
		return false;

	if (buddy_order(buddy) != order)
		return false;

	return true;
}

/*
 * Locate the struct page for both the matching buddy in our
 * pair (buddy1) and the combined O(n+1) page they form (page).
 *
 * 1) Any buddy B1 will have an order O twin B2 which satisfies
 * the following equation:
 *     B2 = B1 ^ (1 << O)
 * For example, if the starting buddy (buddy2) is #8 its order
 * 1 buddy is #10:
 *     B2 = 8 ^ (1 << 1) = 8 ^ 2 = 10
 *
 * 2) Any buddy B will have an order O+1 parent P which
 * satisfies the following equation:
 *     P = B & ~(1 << O)
 *
 * Assumption: *_mem_map is contiguous at least up to MAX_ORDER
 */
static inline unsigned long __find_buddy_pfn(unsigned long page_pfn,
					     unsigned int order)
{
	return page_pfn ^ (1 << order);
}

/*
 * Find the buddy of @page and validate it.
 * @page: The input page
 * @pfn: The pfn of the page, it saves a call to page_to_pfn() when the
 *       function is used in the performance-critical __free_one_page().
 * @order: The order of the page
 * @buddy_pfn: The output pointer to the buddy pfn, it also saves a call to
 *             page_to_pfn().
 *
 * The found buddy can be a non PageBuddy, out of @page's zone, or its order is
 * not the same as @page. The validation is necessary before use it.
 *
 * Return: the found buddy page or NULL if not found.
 */
static inline struct page *find_buddy_page_pfn(struct page *page,
					       unsigned long pfn,
					       unsigned int order,
					       unsigned long *buddy_pfn)
{
	unsigned long __buddy_pfn = __find_buddy_pfn(pfn, order);
	struct page *buddy;

	buddy = page + (__buddy_pfn - pfn);
	if (buddy_pfn)
		*buddy_pfn = __buddy_pfn;

	if (page_is_buddy(page, buddy, order))
		return buddy;
	return NULL;
}

extern void memblock_free_pages(struct page *page, unsigned long pfn,
				unsigned int order);

void __vma_link_list(struct mm_struct *mm, struct vm_area_struct *vma,
		     struct vm_area_struct *prev);

#endif
