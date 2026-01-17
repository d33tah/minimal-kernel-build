
#include <linux/kernel.h>
#include <linux/backing-dev.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/pagevec.h>
#include <linux/shmem_fs.h>
#include <linux/rmap.h>
#include "internal.h"

static inline void __clear_shadow_entry(struct address_space *mapping,
					pgoff_t index, void *entry)
{
	XA_STATE(xas, &mapping->i_pages, index);

	xas_set_update(&xas, NULL);
	if (xas_load(&xas) != entry)
		return;
	xas_store(&xas, NULL);
}

static void clear_shadow_entry(struct address_space *mapping, pgoff_t index,
			       void *entry)
{
	spin_lock(&mapping->host->i_lock);
	xa_lock_irq(&mapping->i_pages);
	__clear_shadow_entry(mapping, index, entry);
	xa_unlock_irq(&mapping->i_pages);
	if (mapping_shrinkable(mapping))
		inode_add_lru(mapping->host);
	spin_unlock(&mapping->host->i_lock);
}

static void truncate_folio_batch_exceptionals(struct address_space *mapping,
					      struct folio_batch *fbatch,
					      pgoff_t *indices)
{
	int i, j;

	for (j = 0; j < folio_batch_count(fbatch); j++)
		if (xa_is_value(fbatch->folios[j]))
			break;

	if (j == folio_batch_count(fbatch))
		return;

	/* dax_mapping() always returns false - S_DAX is 0 */
	spin_lock(&mapping->host->i_lock);
	xa_lock_irq(&mapping->i_pages);

	for (i = j; i < folio_batch_count(fbatch); i++) {
		struct folio *folio = fbatch->folios[i];
		pgoff_t index = indices[i];

		if (!xa_is_value(folio)) {
			fbatch->folios[j++] = folio;
			continue;
		}

		__clear_shadow_entry(mapping, index, folio);
	}

	xa_unlock_irq(&mapping->i_pages);
	if (mapping_shrinkable(mapping))
		inode_add_lru(mapping->host);
	spin_unlock(&mapping->host->i_lock);
	fbatch->nr = j;
}

/* folio_invalidate removed - was empty (a_ops->invalidate_folio never set) */

static void truncate_cleanup_folio(struct folio *folio)
{
	if (folio_mapped(folio))
		unmap_mapping_folio(folio);
	/* folio_invalidate call removed - was empty */
	folio_cancel_dirty(folio);
	folio_clear_mappedtodisk(folio);
}

int truncate_inode_folio(struct address_space *mapping, struct folio *folio)
{
	if (folio->mapping != mapping)
		return -EIO;

	truncate_cleanup_folio(folio);
	filemap_remove_folio(folio);
	return 0;
}

bool truncate_inode_partial_folio(struct folio *folio, loff_t start, loff_t end)
{
	loff_t pos = folio_pos(folio);
	unsigned int offset, length;

	if (pos < start)
		offset = start - pos;
	else
		offset = 0;
	length = folio_size(folio);
	if (pos + length <= (u64)end)
		length = length - offset;
	else
		length = end + 1 - pos - offset;

	folio_wait_writeback(folio);
	if (length == folio_size(folio)) {
		truncate_inode_folio(folio->mapping, folio);
		return true;
	}

	folio_zero_range(folio, offset, length);
	/* folio_invalidate call removed - was empty */
	if (!folio_test_large(folio))
		return true;
	/* split_huge_page always returns 0 - always true */
	return true;
}

/* mapping_evict_folio removed - always returned 0, caller simplified */
/* invalidate_inode_page removed - no callers */

void truncate_inode_pages_range(struct address_space *mapping, loff_t lstart,
				loff_t lend)
{
	pgoff_t start;
	pgoff_t end;
	struct folio_batch fbatch;
	pgoff_t indices[PAGEVEC_SIZE];
	pgoff_t index;
	int i;
	struct folio *folio;
	bool same_folio;

	if (mapping_empty(mapping))
		return;

	start = (lstart + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (lend == -1)

		end = -1;
	else
		end = (lend + 1) >> PAGE_SHIFT;

	folio_batch_init(&fbatch);
	index = start;
	while (index < end &&
	       find_lock_entries(mapping, index, end - 1, &fbatch, indices)) {
		index = indices[folio_batch_count(&fbatch) - 1] + 1;
		truncate_folio_batch_exceptionals(mapping, &fbatch, indices);
		for (i = 0; i < folio_batch_count(&fbatch); i++)
			truncate_cleanup_folio(fbatch.folios[i]);
		delete_from_page_cache_batch(mapping, &fbatch);
		for (i = 0; i < folio_batch_count(&fbatch); i++)
			folio_unlock(fbatch.folios[i]);
		folio_batch_release(&fbatch);
		cond_resched();
	}

	same_folio = (lstart >> PAGE_SHIFT) == (lend >> PAGE_SHIFT);
	folio = __filemap_get_folio(mapping, lstart >> PAGE_SHIFT, FGP_LOCK, 0);
	if (folio) {
		same_folio = lend < folio_pos(folio) + folio_size(folio);
		if (!truncate_inode_partial_folio(folio, lstart, lend)) {
			start = folio->index + folio_nr_pages(folio);
			if (same_folio)
				end = folio->index;
		}
		folio_unlock(folio);
		folio_put(folio);
		folio = NULL;
	}

	if (!same_folio)
		folio = __filemap_get_folio(mapping, lend >> PAGE_SHIFT,
					    FGP_LOCK, 0);
	if (folio) {
		if (!truncate_inode_partial_folio(folio, lstart, lend))
			end = folio->index;
		folio_unlock(folio);
		folio_put(folio);
	}

	index = start;
	while (index < end) {
		cond_resched();
		if (!find_get_entries(mapping, index, end - 1, &fbatch,
				      indices)) {
			if (index == start)
				break;

			index = start;
			continue;
		}

		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];

			index = indices[i];

			if (xa_is_value(folio))
				continue;

			folio_lock(folio);
			folio_wait_writeback(folio);
			truncate_inode_folio(mapping, folio);
			folio_unlock(folio);
			index = folio_index(folio) + folio_nr_pages(folio) - 1;
		}
		truncate_folio_batch_exceptionals(mapping, &fbatch, indices);
		folio_batch_release(&fbatch);
		index++;
	}
}

void truncate_inode_pages(struct address_space *mapping, loff_t lstart)
{
	truncate_inode_pages_range(mapping, lstart, (loff_t)-1);
}

void truncate_inode_pages_final(struct address_space *mapping)
{
	mapping_set_exiting(mapping);

	if (!mapping_empty(mapping)) {
		xa_lock_irq(&mapping->i_pages);
		xa_unlock_irq(&mapping->i_pages);
	}

	truncate_inode_pages(mapping, 0);
}

/* invalidate_mapping_pagevec, invalidate_mapping_pages removed - never called */

static int invalidate_complete_folio2(struct address_space *mapping,
				      struct folio *folio)
{
	if (folio->mapping != mapping)
		return 0;

	if (folio_has_private(folio) &&
	    !filemap_release_folio(folio, GFP_KERNEL))
		return 0;

	spin_lock(&mapping->host->i_lock);
	xa_lock_irq(&mapping->i_pages);
	if (folio_test_dirty(folio))
		goto failed;

	BUG_ON(folio_has_private(folio));
	__filemap_remove_folio(folio, NULL);
	xa_unlock_irq(&mapping->i_pages);
	if (mapping_shrinkable(mapping))
		inode_add_lru(mapping->host);
	spin_unlock(&mapping->host->i_lock);

	filemap_free_folio(mapping, folio);
	return 1;
failed:
	xa_unlock_irq(&mapping->i_pages);
	spin_unlock(&mapping->host->i_lock);
	return 0;
}

int invalidate_inode_pages2_range(struct address_space *mapping, pgoff_t start,
				  pgoff_t end)
{
	pgoff_t indices[PAGEVEC_SIZE];
	struct folio_batch fbatch;
	pgoff_t index;
	int i;
	int ret = 0;
	int ret2 = 0;
	int did_range_unmap = 0;

	if (mapping_empty(mapping))
		return 0;

	folio_batch_init(&fbatch);
	index = start;
	while (find_get_entries(mapping, index, end, &fbatch, indices)) {
		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];

			index = indices[i];

			if (xa_is_value(folio)) {
				clear_shadow_entry(mapping, index, folio);
				continue;
			}

			if (!did_range_unmap && folio_mapped(folio)) {
				unmap_mapping_pages(mapping, index,
						    (1 + end - index), false);
				did_range_unmap = 1;
			}

			folio_lock(folio);
			if (folio->mapping != mapping) {
				folio_unlock(folio);
				continue;
			}
			folio_wait_writeback(folio);

			if (folio_mapped(folio))
				unmap_mapping_folio(folio);
			BUG_ON(folio_mapped(folio));

			/* launder_folio check removed - never set */
			ret2 = 0;
			if (!invalidate_complete_folio2(mapping, folio))
				ret2 = -EBUSY;
			if (ret2 < 0)
				ret = ret2;
			folio_unlock(folio);
		}
		folio_batch_remove_exceptionals(&fbatch);
		folio_batch_release(&fbatch);
		cond_resched();
		index++;
	}

	/* dax_mapping() always returns false */
	return ret;
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

	/* page_mkclean always returns 0 - dead code removed */
	unlock_page(page);
	put_page(page);
}
