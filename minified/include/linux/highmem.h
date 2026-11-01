/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HIGHMEM_H
#define _LINUX_HIGHMEM_H

#include <asm/cacheflush.h>
struct folio;
#include <linux/uaccess.h>

static inline void *kmap_local_page(struct page *page)
{
	return page_address(page);
}

static inline void *kmap_atomic(struct page *page)
{
	preempt_disable();
	pagefault_disable();
	return page_address(page);
}

static inline void __kunmap_atomic(void *addr)
{
	pagefault_enable();
	preempt_enable();
}

#define kunmap_atomic(__addr)                                       \
	do {                                                        \
		BUILD_BUG_ON(__same_type((__addr), struct page *)); \
		__kunmap_atomic(__addr);                            \
	} while (0)

static inline void zero_user_segments(struct page *page, unsigned start1,
				      unsigned end1, unsigned start2,
				      unsigned end2)
{
	void *kaddr = kmap_local_page(page);

	BUG_ON(end1 > page_size(page) || end2 > page_size(page));

	if (end1 > start1)
		memset(kaddr + start1, 0, end1 - start1);

	if (end2 > start2)
		memset(kaddr + start2, 0, end2 - start2);
}

static inline void zero_user(struct page *page, unsigned start, unsigned size)
{
	zero_user_segments(page, start, start + size, 0, 0);
}

/**
 * folio_zero_range() - Zero a byte range in a folio.
 * @folio: The folio to write to.
 * @start: The first byte to zero.
 * @length: The number of bytes to zero.
 */
static inline void folio_zero_range(struct folio *folio, size_t start,
				    size_t length)
{
	zero_user_segments(&folio->page, start, start + length, 0, 0);
}

#endif /* _LINUX_HIGHMEM_H */
