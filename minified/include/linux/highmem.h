#ifndef _LINUX_HIGHMEM_H
#define _LINUX_HIGHMEM_H

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/cacheflush.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>

#include "highmem-internal.h"

static inline void *kmap(struct page *page);

static inline void kunmap(struct page *page);

static inline struct page *kmap_to_page(void *addr);

static inline void kmap_flush_unused(void);

static inline void *kmap_local_page(struct page *page);

static inline void *kmap_local_folio(struct folio *folio, size_t offset);

static inline void *kmap_atomic(struct page *page);

static inline unsigned int nr_free_highpages(void);
static inline unsigned long totalhigh_pages(void);

#ifndef ARCH_HAS_FLUSH_ANON_PAGE
static inline void flush_anon_page(struct vm_area_struct *vma, struct page *page, unsigned long vmaddr)
{
}
#endif

/* flush_kernel_vmap_range, invalidate_kernel_vmap_range removed - unused */

#ifndef clear_user_highpage
static inline void clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *addr = kmap_local_page(page);
	clear_user_page(addr, vaddr, page);
	kunmap_local(addr);
}
#endif

#ifndef __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE_MOVABLE
static inline struct page *
alloc_zeroed_user_highpage_movable(struct vm_area_struct *vma,
				   unsigned long vaddr)
{
	struct page *page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vaddr);

	if (page)
		clear_user_highpage(page, vaddr);

	return page;
}
#endif

/* clear_highpage, tag_clear_highpage removed - unused */

static inline void zero_user_segments(struct page *page,
		unsigned start1, unsigned end1,
		unsigned start2, unsigned end2)
{
	void *kaddr = kmap_local_page(page);
	unsigned int i;

	BUG_ON(end1 > page_size(page) || end2 > page_size(page));

	if (end1 > start1)
		memset(kaddr + start1, 0, end1 - start1);

	if (end2 > start2)
		memset(kaddr + start2, 0, end2 - start2);

	kunmap_local(kaddr);
	for (i = 0; i < compound_nr(page); i++)
		flush_dcache_page(page + i);
}

static inline void zero_user_segment(struct page *page,
	unsigned start, unsigned end)
{
	zero_user_segments(page, start, end, 0, 0);
}

static inline void zero_user(struct page *page,
	unsigned start, unsigned size)
{
	zero_user_segments(page, start, start + size, 0, 0);
}

#ifndef __HAVE_ARCH_COPY_USER_HIGHPAGE

static inline void copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma)
{
	char *vfrom, *vto;

	vfrom = kmap_local_page(from);
	vto = kmap_local_page(to);
	copy_user_page(vto, vfrom, vaddr, to);
	kunmap_local(vto);
	kunmap_local(vfrom);
}

#endif

static inline void memcpy_to_page(struct page *page, size_t offset,
				  const char *from, size_t len)
{
	char *to = kmap_local_page(page);

	VM_BUG_ON(offset + len > PAGE_SIZE);
	memcpy(to + offset, from, len);
	flush_dcache_page(page);
	kunmap_local(to);
}

static inline void folio_zero_range(struct folio *folio,
		size_t start, size_t length)
{
	zero_user_segments(&folio->page, start, start + length, 0, 0);
}

#endif  
