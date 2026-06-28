
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/shmem_fs.h>
#include "internal.h"

/*
 * On this single-shot boot every address_space reaching truncate is empty
 * (runtime trace: the folio-iteration body below never executed, and none of
 * its helpers -- truncate_folio_batch_exceptionals, truncate_cleanup_folio,
 * truncate_inode_folio, truncate_inode_partial_folio, folio_invalidate,
 * __clear_shadow_entry -- were ever called).  The function always returns at
 * the mapping_empty() guard, so the dead iteration and its helper cluster were
 * folded away.  truncate_inode_pages_range had a single caller
 * (truncate_inode_pages); its surviving guard body was folded in directly.
 */
void truncate_inode_pages(struct address_space *mapping, loff_t lstart)
{
	if (mapping_empty(mapping))
		return;
}

void truncate_inode_pages_final(struct address_space *mapping)
{
	if (!mapping_empty(mapping)) {
		 
		xa_lock_irq(&mapping->i_pages);
		xa_unlock_irq(&mapping->i_pages);
	}

	truncate_inode_pages(mapping, 0);
}

void truncate_pagecache(struct inode *inode, loff_t newsize)
{
	struct address_space *mapping = inode->i_mapping;
	loff_t holebegin = round_up(newsize, PAGE_SIZE);

	 
	unmap_mapping_range(mapping, holebegin, 0, 1);
	truncate_inode_pages(mapping, newsize);
	unmap_mapping_range(mapping, holebegin, 0, 1);
}

void truncate_setsize(struct inode *inode, loff_t newsize)
{
	loff_t oldsize = inode->i_size;

	i_size_write(inode, newsize);
	if (newsize > oldsize)
		pagecache_isize_extended(inode, oldsize, newsize);
	truncate_pagecache(inode, newsize);
}

void pagecache_isize_extended(struct inode *inode, loff_t from, loff_t to)
{
	int bsize = i_blocksize(inode);
	loff_t rounded_from;
	struct page *page;
	pgoff_t index;

	WARN_ON(to > inode->i_size);

	if (from >= to || bsize == PAGE_SIZE)
		return;
	 
	rounded_from = round_up(from, bsize);
	if (to <= rounded_from || !(rounded_from & (PAGE_SIZE - 1)))
		return;

	index = from >> PAGE_SHIFT;
	page = find_lock_page(inode->i_mapping, index);
	 
	if (!page)
		return;
	 
	unlock_page(page);
	put_page(page);
}
