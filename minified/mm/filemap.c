
#include <linux/swap.h>
#include <linux/uio.h>
#include <linux/backing-dev.h>
#ifndef _LINUX_PAGEVEC_H
#define _LINUX_PAGEVEC_H
#define PAGEVEC_SIZE 15
struct page;
struct folio;
struct address_space;
struct pagevec {
	unsigned char nr;
	bool percpu_pvec_drained;
	struct page *pages[PAGEVEC_SIZE];
};
struct folio_batch {
	unsigned char nr;
	bool percpu_pvec_drained;
	struct folio *folios[PAGEVEC_SIZE];
};
static_assert(sizeof(struct pagevec) == sizeof(struct folio_batch));
static_assert(offsetof(struct pagevec, pages) ==
	      offsetof(struct folio_batch, folios));
static inline void folio_batch_init(struct folio_batch *fbatch)
{
	fbatch->nr = 0;
	fbatch->percpu_pvec_drained = false;
}
static inline unsigned int folio_batch_count(struct folio_batch *fbatch)
{
	return fbatch->nr;
}
static inline unsigned int fbatch_space(struct folio_batch *fbatch)
{
	return PAGEVEC_SIZE - fbatch->nr;
}
static inline unsigned folio_batch_add(struct folio_batch *fbatch,
				       struct folio *folio)
{
	fbatch->folios[fbatch->nr++] = folio;
	return fbatch_space(fbatch);
}
#endif
#include "internal.h"

static noinline int __filemap_add_folio(struct address_space *mapping,
					struct folio *folio, pgoff_t index,
					gfp_t gfp, void **shadowp)
{
	XA_STATE(xas, &mapping->i_pages, index);
	long nr;

	mapping_set_update(&xas, mapping);

	xas_set_order(&xas, index, folio_order(folio));
	nr = folio_nr_pages(folio);

	gfp &= GFP_RECLAIM_MASK;
	atomic_add(nr, &folio->page._refcount);
	folio->mapping = mapping;
	folio->index = xas.xa_index;

	do {
		/* xas_find_conflict returns NULL, so loop never iterates */
		xas_lock_irq(&xas);

		xas_store(&xas, folio);
		if (xas_error(&xas))
			goto unlock;

		__lruvec_stat_mod_folio(folio, NR_FILE_PAGES, nr);
unlock:
		xas_unlock_irq(&xas);
	} while (xas_nomem(&xas, gfp));

	if (xas_error(&xas))
		goto error;

	return 0;
error:
	folio->mapping = NULL;

	if (atomic_sub_and_test(nr, &folio->page._refcount))
		__put_page(&folio->page);
	return xas_error(&xas);
}

static int filemap_add_folio(struct address_space *mapping, struct folio *folio,
			     pgoff_t index, gfp_t gfp)
{
	void *shadow = NULL;
	int ret;

	__folio_set_locked(folio);
	ret = __filemap_add_folio(mapping, folio, index, gfp, &shadow);
	if (unlikely(ret))
		__folio_clear_locked(folio);
	else {
		WARN_ON_ONCE(folio_test_active(folio));
		folio_add_lru(folio);
	}
	return ret;
}

#define PAGE_WAIT_TABLE_BITS 4 /* Reduced from 8 for minimal boot */
#define PAGE_WAIT_TABLE_SIZE (1 << PAGE_WAIT_TABLE_BITS)
static wait_queue_head_t
	folio_wait_table[PAGE_WAIT_TABLE_SIZE] __cacheline_aligned;

static wait_queue_head_t *folio_waitqueue(struct folio *folio)
{
	return &folio_wait_table[hash_ptr(folio, PAGE_WAIT_TABLE_BITS)];
}

void __init pagecache_init(void)
{
	int i;

	for (i = 0; i < PAGE_WAIT_TABLE_SIZE; i++)
		init_waitqueue_head(&folio_wait_table[i]);
}

static void folio_wake_bit(struct folio *folio, int bit_nr)
{
	wait_queue_head_t *q = folio_waitqueue(folio);
	struct wait_page_key key;
	unsigned long flags;
	wait_queue_entry_t bookmark;

	key.folio = folio;
	key.bit_nr = bit_nr;
	key.page_match = 0;

	bookmark.flags = 0;
	bookmark.private = NULL;
	bookmark.func = NULL;
	INIT_LIST_HEAD(&bookmark.entry);

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_locked_key_bookmark(q, TASK_NORMAL, &key, &bookmark);

	while (bookmark.flags & WQ_FLAG_BOOKMARK) {
		spin_unlock_irqrestore(&q->lock, flags);
		cpu_relax();
		spin_lock_irqsave(&q->lock, flags);
		__wake_up_locked_key_bookmark(q, TASK_NORMAL, &key, &bookmark);
	}

	if (!waitqueue_active(q) || !key.page_match)
		folio_clear_waiters(folio);

	spin_unlock_irqrestore(&q->lock, flags);
}

void folio_unlock(struct folio *folio)
{
	BUILD_BUG_ON(PG_waiters != 4);
	BUILD_BUG_ON(PG_locked > 4);
	clear_bit_unlock(PG_locked, folio_flags(folio, 0));
	if (test_bit(PG_waiters, folio_flags(folio, 0)))
		folio_wake_bit(folio, PG_locked);
}

static void filemap_get_read_batch(struct address_space *mapping, pgoff_t index,
				   pgoff_t max, struct folio_batch *fbatch)
{
	XA_STATE(xas, &mapping->i_pages, index);
	struct folio *folio;

	rcu_read_lock();
	for (folio = xas_load(&xas); folio; folio = xas_next(&xas)) {
		if (xas_retry(&xas, folio))
			continue;
		if (xas.xa_index > max || xa_is_value(folio))
			break;
		if (!folio_try_get_rcu(folio))
			goto retry;

		if (unlikely(folio != xas_reload(&xas)))
			goto put_folio;

		if (!folio_batch_add(fbatch, folio))
			break;
		if (!folio_test_uptodate(folio))
			break;
		if (folio_test_readahead(folio))
			break;
		xas_advance(&xas, folio->index + folio_nr_pages(folio) - 1);
		continue;
put_folio:
		folio_put(folio);
retry:
		xas_reset(&xas);
	}
	rcu_read_unlock();
}

static int filemap_read_folio(struct file *file, struct address_space *mapping,
			      struct folio *folio)
{
	int error;

	folio_clear_error(folio);

	error = mapping->a_ops->read_folio(file, folio);
	if (error)
		return error;

	error = folio_wait_locked_killable(folio);
	if (error)
		return error;
	if (folio_test_uptodate(folio))
		return 0;
	return -EIO;
}

static int filemap_update_page(struct kiocb *iocb,
			       struct address_space *mapping,
			       struct iov_iter *iter, struct folio *folio)
{
	int error;

	filemap_invalidate_lock_shared(mapping);

	if (!folio_trylock(folio)) {
		filemap_invalidate_unlock_shared(mapping);
		return AOP_TRUNCATED_PAGE;
	}

	if (!folio->mapping) {
		error = AOP_TRUNCATED_PAGE;
		goto unlock;
	}
	if (folio_test_uptodate(folio)) {
		error = 0;
		goto unlock;
	}

	error = filemap_read_folio(iocb->ki_filp, mapping, folio);
	filemap_invalidate_unlock_shared(mapping);
	if (error == AOP_TRUNCATED_PAGE)
		folio_put(folio);
	return error;
unlock:
	folio_unlock(folio);
	filemap_invalidate_unlock_shared(mapping);
	if (error == AOP_TRUNCATED_PAGE)
		folio_put(folio);
	return error;
}

static int filemap_get_pages(struct kiocb *iocb, struct iov_iter *iter,
			     struct folio_batch *fbatch)
{
	struct file *filp = iocb->ki_filp;
	struct address_space *mapping = filp->f_mapping;
	pgoff_t index = iocb->ki_pos >> PAGE_SHIFT;
	pgoff_t last_index;
	struct folio *folio;
	int err = 0;

	last_index = DIV_ROUND_UP(iocb->ki_pos + iter->count, PAGE_SIZE);
retry:
	if (fatal_signal_pending(current))
		return -EINTR;

	filemap_get_read_batch(mapping, index, last_index, fbatch);
	if (!folio_batch_count(fbatch)) {
		folio = filemap_alloc_folio(mapping_gfp_mask(mapping), 0);
		if (!folio)
			return -ENOMEM;
		filemap_invalidate_lock_shared(mapping);
		err = filemap_add_folio(mapping, folio, index,
					mapping_gfp_constraint(mapping,
							       GFP_KERNEL));
		if (err == -EEXIST)
			err = AOP_TRUNCATED_PAGE;
		if (!err)
			err = filemap_read_folio(filp, mapping, folio);
		if (err) {
			filemap_invalidate_unlock_shared(mapping);
			folio_put(folio);
			if (err == AOP_TRUNCATED_PAGE)
				goto retry;
			return err;
		}
		filemap_invalidate_unlock_shared(mapping);
		folio_batch_add(fbatch, folio);
		return 0;
	}

	folio = fbatch->folios[folio_batch_count(fbatch) - 1];
	if (!folio_test_uptodate(folio)) {
		err = filemap_update_page(iocb, mapping, iter, folio);
		if (err)
			goto err;
	}

	return 0;
err:
	if (err < 0)
		folio_put(folio);
	if (likely(--fbatch->nr))
		return 0;
	if (err == AOP_TRUNCATED_PAGE)
		goto retry;
	return err;
}

static ssize_t filemap_read(struct kiocb *iocb, struct iov_iter *iter,
			    ssize_t already_read)
{
	struct file *filp = iocb->ki_filp;
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = mapping->host;
	struct folio_batch fbatch;
	int i, error = 0;
	loff_t isize, end_offset;

	if (unlikely(iocb->ki_pos >= inode->i_sb->s_maxbytes))
		return 0;
	if (unlikely(!iov_iter_count(iter)))
		return 0;

	iov_iter_truncate(iter, inode->i_sb->s_maxbytes);
	folio_batch_init(&fbatch);

	do {
		cond_resched();

		if (unlikely(iocb->ki_pos >= i_size_read(inode)))
			break;

		error = filemap_get_pages(iocb, iter, &fbatch);
		if (error < 0)
			break;

		isize = i_size_read(inode);
		if (unlikely(iocb->ki_pos >= isize))
			goto put_folios;
		end_offset = min_t(loff_t, isize, iocb->ki_pos + iter->count);

		{
		}

		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];
			size_t fsize = folio_size(folio);
			size_t offset = iocb->ki_pos & (fsize - 1);
			size_t bytes = min_t(loff_t, end_offset - iocb->ki_pos,
					     fsize - offset);
			size_t copied;

			if (end_offset < folio_pos(folio))
				break;

			copied = copy_folio_to_iter(folio, offset, bytes, iter);

			already_read += copied;
			iocb->ki_pos += copied;

			if (copied < bytes) {
				error = -EFAULT;
				break;
			}
		}
put_folios:
		for (i = 0; i < folio_batch_count(&fbatch); i++)
			folio_put(fbatch.folios[i]);
		folio_batch_init(&fbatch);
	} while (iov_iter_count(iter) && iocb->ki_pos < isize && !error);

	return already_read ? already_read : error;
}

ssize_t generic_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	size_t count = iov_iter_count(iter);

	if (!count)
		return 0;

	return filemap_read(iocb, iter, 0);
}

/* Merged from folio-compat.c */
bool set_page_dirty(struct page *page)
{
	return folio_mark_dirty(page_folio(page));
}
