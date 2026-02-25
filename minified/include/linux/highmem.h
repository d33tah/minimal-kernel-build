#ifndef _LINUX_HIGHMEM_H
#define _LINUX_HIGHMEM_H

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/bug.h>
struct page;
struct folio;
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>

static inline void kmap_local_fork(struct task_struct *tsk) {}

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

static inline void folio_zero_range(struct folio *folio,
		size_t start, size_t length)
{
	void *kaddr = kmap_local_page(&folio->page);
	memset(kaddr + start, 0, length);
	kunmap_local(kaddr);
}

#endif  
