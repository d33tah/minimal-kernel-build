#ifndef _LINUX_HIGHMEM_H
#define _LINUX_HIGHMEM_H

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/bug.h>
/* cacheflush.h inlined */
#include <asm/cacheflush.h>
struct folio;
#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
#ifndef ARCH_IMPLEMENTS_FLUSH_DCACHE_FOLIO
void flush_dcache_folio(struct folio *folio);
#endif
#else
static inline void flush_dcache_folio(struct folio *folio) {}
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_FOLIO 0
#endif
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>

/* Inlined from highmem-internal.h */
void *__kmap_local_pfn_prot(unsigned long pfn, pgprot_t prot);
void *__kmap_local_page_prot(struct page *page, pgprot_t prot);
/* kunmap_local_indexed removed - unused */
void kmap_local_fork(struct task_struct *tsk);
void __kmap_local_sched_out(void);
void __kmap_local_sched_in(void);
static inline void kmap_assert_nomap(void)
{
	DEBUG_LOCKS_WARN_ON(current->kmap_ctrl.idx);
}

static inline struct page *kmap_to_page(void *addr)
{
	return virt_to_page(addr);
}

static inline void *kmap(struct page *page)
{
	might_sleep();
	return page_address(page);
}

static inline void kunmap_high(struct page *page) { }

static inline void kunmap(struct page *page)
{
#ifdef ARCH_HAS_FLUSH_ON_KUNMAP
	kunmap_flush_on_unmap(page_address(page));
#endif
}

static inline void *kmap_local_page(struct page *page)
{
	return page_address(page);
}

static inline void *kmap_local_folio(struct folio *folio, size_t offset)
{
	return page_address(&folio->page) + offset;
}

static inline void *kmap_local_page_prot(struct page *page, pgprot_t prot)
{
	return kmap_local_page(page);
}

static inline void *kmap_local_pfn(unsigned long pfn)
{
	return kmap_local_page(pfn_to_page(pfn));
}

static inline void __kunmap_local(void *addr)
{
#ifdef ARCH_HAS_FLUSH_ON_KUNMAP
	kunmap_flush_on_unmap(addr);
#endif
}

static inline void *kmap_atomic(struct page *page)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		migrate_disable();
	else
		preempt_disable();
	pagefault_disable();
	return page_address(page);
}

static inline void __kunmap_atomic(void *addr)
{
#ifdef ARCH_HAS_FLUSH_ON_KUNMAP
	kunmap_flush_on_unmap(addr);
#endif
	pagefault_enable();
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		migrate_enable();
	else
		preempt_enable();
}

#define kunmap_atomic(__addr)					\
do {								\
	BUILD_BUG_ON(__same_type((__addr), struct page *));	\
	__kunmap_atomic(__addr);				\
} while (0)

#define kunmap_local(__addr)					\
do {								\
	BUILD_BUG_ON(__same_type((__addr), struct page *));	\
	__kunmap_local(__addr);					\
} while (0)

static inline void kmap_flush_unused(void) { }
static inline unsigned int nr_free_highpages(void) { return 0; }
static inline unsigned long totalhigh_pages(void) { return 0; }

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
