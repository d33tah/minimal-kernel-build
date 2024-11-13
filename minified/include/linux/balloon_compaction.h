/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/balloon_compaction.h
 *
 * Common interface definitions for making balloon pages movable by compaction.
 *
 * Balloon page migration makes use of the general non-lru movable page
 * feature.
 *
 * page->private is used to reference the responsible balloon device.
 * page->mapping is used in context of non-lru page migration to reference
 * the address space operations for page isolation/migration/compaction.
 *
 * As the page isolation scanning step a compaction thread does is a lockless
 * procedure (from a page standpoint), it might bring some racy situations while
 * performing balloon page compaction. In order to sort out these racy scenarios
 * and safely perform balloon's page compaction and migration we must, always,
 * ensure following these simple rules:
 *
 *   i. when updating a balloon's page ->mapping element, strictly do it under
 *      the following lock order, independently of the far superior
 *      locking scheme (lru_lock, balloon_lock):
 *	    +-page_lock(page);
 *	      +--spin_lock_irq(&b_dev_info->pages_lock);
 *	            ... page->mapping updates here ...
 *
 *  ii. isolation or dequeueing procedure must remove the page from balloon
 *      device page list under b_dev_info->pages_lock.
 *
 * The functions provided by this interface are placed to help on coping with
 * the aforementioned balloon page corner case, as well as to ensure the simple
 * set of exposed rules are satisfied while we are dealing with balloon pages
 * compaction / migration.
 *
 * Copyright (C) 2012, Red Hat, Inc.  Rafael Aquini <aquini@redhat.com>
 */
#ifndef _LINUX_BALLOON_COMPACTION_H
#define _LINUX_BALLOON_COMPACTION_H
#include <linux/pagemap.h>
#include <linux/page-flags.h>
#include <linux/migrate.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/list.h>

/*
 * Balloon device information descriptor.
 * This struct is used to allow the common balloon compaction interface
 * procedures to find the proper balloon device holding memory pages they'll
 * have to cope for page compaction / migration, as well as it serves the
 * balloon driver as a page book-keeper for its registered balloon devices.
 */
struct balloon_dev_info {
	unsigned long isolated_pages;	/* # of isolated pages for migration */
	spinlock_t pages_lock;		/* Protection to pages list */
	struct list_head pages;		/* Pages enqueued & handled to Host */
	int (*migratepage)(struct balloon_dev_info *, struct page *newpage,
			struct page *page, enum migrate_mode mode);
	struct inode *inode;
};

extern struct page *balloon_page_alloc(void);
extern void balloon_page_enqueue(struct balloon_dev_info *b_dev_info,
				 struct page *page);
extern struct page *balloon_page_dequeue(struct balloon_dev_info *b_dev_info);
extern size_t balloon_page_list_enqueue(struct balloon_dev_info *b_dev_info,
				      struct list_head *pages);
extern size_t balloon_page_list_dequeue(struct balloon_dev_info *b_dev_info,
				     struct list_head *pages, size_t n_req_pages);

static inline void balloon_devinfo_init(struct balloon_dev_info *balloon)
{
	balloon->isolated_pages = 0;
	spin_lock_init(&balloon->pages_lock);
	INIT_LIST_HEAD(&balloon->pages);
	balloon->migratepage = NULL;
	balloon->inode = NULL;
}


static inline void balloon_page_insert(struct balloon_dev_info *balloon,
				       struct page *page)
{
	__SetPageOffline(page);
	list_add(&page->lru, &balloon->pages);
}

static inline void balloon_page_delete(struct page *page)
{
	__ClearPageOffline(page);
	list_del(&page->lru);
}

static inline gfp_t balloon_mapping_gfp_mask(void)
{
	return GFP_HIGHUSER;
}


/*
 * balloon_page_push - insert a page into a page list.
 * @head : pointer to list
 * @page : page to be added
 *
 * Caller must ensure the page is private and protect the list.
 */
static inline void balloon_page_push(struct list_head *pages, struct page *page)
{
	list_add(&page->lru, pages);
}

/*
 * balloon_page_pop - remove a page from a page list.
 * @head : pointer to list
 * @page : page to be added
 *
 * Caller must ensure the page is private and protect the list.
 */
static inline struct page *balloon_page_pop(struct list_head *pages)
{
	struct page *page = list_first_entry_or_null(pages, struct page, lru);

	if (!page)
		return NULL;

	list_del(&page->lru);
	return page;
}
#endif /* _LINUX_BALLOON_COMPACTION_H */
