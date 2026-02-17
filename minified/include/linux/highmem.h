#ifndef _LINUX_HIGHMEM_H
#define _LINUX_HIGHMEM_H

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/bug.h>
struct page;
static inline void flush_dcache_page(struct page *page) { }
struct folio;
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>

void kmap_local_fork(struct task_struct *tsk);
void __kmap_local_sched_out(void);
void __kmap_local_sched_in(void);

static inline void kunmap(struct page *page)
{
}

static inline void *kmap_local_page(struct page *page)
{
	return page_address(page);
}

static inline void __kunmap_local(void *addr)
{
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

static inline void zero_user(struct page *page,
	unsigned start, unsigned size)
{
	zero_user_segments(page, start, start + size, 0, 0);
}

static inline void folio_zero_range(struct folio *folio,
		size_t start, size_t length)
{
	zero_user_segments(&folio->page, start, start + length, 0, 0);
}

#endif  
