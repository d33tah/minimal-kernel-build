#include <linux/uio.h>
#include <linux/pagevec.h>
#include "internal.h"

static noinline int __filemap_add_folio(struct address_space *mapping,
					struct folio *folio, pgoff_t index,
					gfp_t gfp, void **shadowp)
{
	XA_STATE(xas, &mapping->i_pages, index);
	long nr = 1;

	{
		xas_set_order(&xas, index, folio_order(folio));
		nr = folio_nr_pages(folio);
	}

	folio_ref_add(folio, nr);
	folio->mapping = mapping;
	folio->index = xas.xa_index;

	xas_lock_irq(&xas);

	xas_store(&xas, folio);

	xas_unlock_irq(&xas);

	return 0;
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
	return ret;
}

/*
 * Nothing can wait on a folio bit (no code in this kernel puts a task to
 * sleep: there are no wait queues), so there is no PG_waiters flag and unlock
 * only clears PG_locked (the "negative byte" result is ignored).
 */
void folio_unlock(struct folio *folio)
{
	clear_bit_unlock_is_negative_byte(PG_locked, folio_flags(folio, 0));
}

/*
 * folio_lock() lands here only when folio_trylock() failed. Upstream sleeps
 * in folio_wait_bit_common() until the holder unlocks; this kernel has no
 * wait queues and no second task that could hold the lock, so contention is
 * a bug: fail loudly instead of continuing without the lock.
 */
void __folio_lock(struct folio *folio)
{
	BUG();
}

static void *mapping_get_entry(struct address_space *mapping, pgoff_t index)
{
	XA_STATE(xas, &mapping->i_pages, index);
	struct folio *folio;

	rcu_read_lock();
repeat:
	xas_reset(&xas);
	folio = xas_load(&xas);

	if (!folio)
		goto out;

	if (!folio_try_get_rcu(folio))
		goto repeat;

	if (unlikely(folio != xas_reload(&xas))) {
		folio_put(folio);
		goto repeat;
	}
out:
	rcu_read_unlock();

	return folio;
}

struct folio *__filemap_get_folio(struct address_space *mapping, pgoff_t index,
				  int fgp_flags, gfp_t gfp)
{
	struct folio *folio;

repeat:
	folio = mapping_get_entry(mapping, index);
	if (!folio)
		goto no_page;

	if (fgp_flags & FGP_LOCK) {
		folio_lock(folio);

		if (unlikely(folio->mapping != mapping)) {
			folio_unlock(folio);
			folio_put(folio);
			goto repeat;
		}
	}

no_page:
	if (!folio && (fgp_flags & FGP_CREAT)) {
		int err;

		folio = filemap_alloc_folio(gfp, 0);
		if (!folio)
			return NULL;

		err = filemap_add_folio(mapping, folio, index, gfp);
		if (unlikely(err)) {
			folio_put(folio);
			folio = NULL;
			if (err == -EEXIST)
				goto repeat;
		}

		if (folio && (fgp_flags & FGP_FOR_MMAP))
			folio_unlock(folio);
	}

	return folio;
}

static void filemap_get_read_batch(struct address_space *mapping, pgoff_t index,
				   pgoff_t max, struct folio_batch *fbatch)
{
	XA_STATE(xas, &mapping->i_pages, index);
	struct folio *folio;

	rcu_read_lock();
	for (folio = xas_load(&xas); folio; folio = xas_next(&xas)) {
		if (xas.xa_index > max)
			break;
		/* CONFIG_XARRAY_MULTI off: xa_is_sibling(folio) compile-time false */
		if (!folio_try_get_rcu(folio))
			goto retry;

		if (unlikely(folio != xas_reload(&xas)))
			goto put_folio;

		if (!folio_batch_add(fbatch, folio))
			break;
		if (!folio_test_uptodate(folio))
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

	error = mapping->a_ops->read_folio(file, folio);
	if (error)
		return error;

	if (folio_test_uptodate(folio))
		return 0;
	return -EIO;
}

static int filemap_create_folio(struct file *file,
				struct address_space *mapping, pgoff_t index,
				struct folio_batch *fbatch)
{
	struct folio *folio;
	int error;

	folio = filemap_alloc_folio(mapping_gfp_mask(mapping), 0);
	if (!folio)
		return -ENOMEM;

	filemap_invalidate_lock_shared(mapping);
	error = filemap_add_folio(mapping, folio, index,
				  mapping_gfp_constraint(mapping, GFP_KERNEL));
	if (error == -EEXIST)
		error = AOP_TRUNCATED_PAGE;
	if (error)
		goto error;

	error = filemap_read_folio(file, mapping, folio);
	if (error)
		goto error;

	filemap_invalidate_unlock_shared(mapping);
	folio_batch_add(fbatch, folio);
	return 0;
error:
	filemap_invalidate_unlock_shared(mapping);
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
	filemap_get_read_batch(mapping, index, last_index, fbatch);
	if (!folio_batch_count(fbatch)) {
		err = filemap_create_folio(filp, mapping,
					   iocb->ki_pos >> PAGE_SHIFT, fbatch);
		if (err == AOP_TRUNCATED_PAGE)
			goto retry;
		return err;
	}

	/*
	 * ramfs is the only filesystem: its folios are uptodate from the
	 * moment write_end or read_folio unlocks them, so the read path never
	 * has to bring a cached folio uptodate.
	 */
	folio = fbatch->folios[folio_batch_count(fbatch) - 1];
	if (WARN_ON_ONCE(!folio_test_uptodate(folio))) {
		folio_put(folio);
		if (likely(--fbatch->nr))
			return 0;
		return -EIO;
	}

	return 0;
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

	folio_batch_init(&fbatch);

	do {
		if (unlikely(iocb->ki_pos >= i_size_read(inode)))
			break;

		error = filemap_get_pages(iocb, iter, &fbatch);
		if (error < 0)
			break;

		isize = i_size_read(inode);
		if (unlikely(iocb->ki_pos >= isize))
			goto put_folios;
		end_offset = min_t(loff_t, isize, iocb->ki_pos + iter->count);

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
	ssize_t retval = 0;

	if (!count)
		return 0;

	return filemap_read(iocb, iter, retval);
}

static vm_fault_t filemap_fault(struct vm_fault *vmf)
{
	int error;
	struct file *file = vmf->vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	pgoff_t max_idx, index = vmf->pgoff;
	struct folio *folio;

	max_idx = DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE);
	if (unlikely(index >= max_idx))
		return VM_FAULT_SIGBUS;

retry_find:
	folio = __filemap_get_folio(mapping, index, FGP_CREAT | FGP_FOR_MMAP,
				    vmf->gfp_mask);
	if (!folio)
		return VM_FAULT_OOM;

	if (!folio_test_locked(folio))
		folio_lock(folio);

	/*
	 * We have a locked page in the page cache, now we need to check
	 * that it's up-to-date. If not, it is going to be due to an error.
	 */
	if (unlikely(!folio_test_uptodate(folio)))
		goto page_not_uptodate;

	vmf->page = folio_file_page(folio, index);
	return VM_FAULT_LOCKED;

page_not_uptodate:
	/*
	 * Umm, take care of errors if the page isn't up-to-date.
	 * Try to re-read it _once_. We do this synchronously,
	 * because there really aren't any performance issues here
	 * and we need to check for errors.
	 */
	error = filemap_read_folio(file, mapping, folio);
	folio_put(folio);

	if (!error || error == AOP_TRUNCATED_PAGE)
		goto retry_find;

	return VM_FAULT_SIGBUS;
}

static const struct vm_operations_struct generic_file_vm_ops = {
	.fault = filemap_fault,
};

int generic_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct address_space *mapping = file->f_mapping;

	if (!mapping->a_ops->read_folio)
		return -ENOEXEC;
	vma->vm_ops = &generic_file_vm_ops;
	return 0;
}

static ssize_t generic_perform_write(struct kiocb *iocb, struct iov_iter *i)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	const struct address_space_operations *a_ops = mapping->a_ops;
	loff_t pos = iocb->ki_pos;
	ssize_t written = 0;

	/* One page-cache page per step, at the offset within that page. */
	while (iov_iter_count(i)) {
		unsigned long offset = offset_in_page(pos);
		size_t bytes =
			min_t(size_t, PAGE_SIZE - offset, iov_iter_count(i));
		struct page *page;
		void *fsdata;
		int status;

		status = a_ops->write_begin(file, mapping, pos, bytes, &page,
					    &fsdata);
		if (status < 0)
			return written ? written : status;
		bytes = copy_page_from_iter_atomic(page, offset, bytes, i);
		a_ops->write_end(file, mapping, pos, bytes, bytes, page,
				 fsdata);
		if (!bytes)
			return written ? written : -EFAULT;
		pos += bytes;
		written += bytes;
	}
	return written;
}

static ssize_t __generic_file_write_iter(struct kiocb *iocb,
					 struct iov_iter *from)
{
	/* No set-id bits to strip and no timestamp updates on this build. */
	ssize_t written = generic_perform_write(iocb, from);

	if (likely(written > 0))
		iocb->ki_pos += written;
	return written;
}

ssize_t generic_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ssize_t ret;

	inode_lock(inode);
	ret = generic_write_checks(iocb, from);
	if (ret > 0)
		ret = __generic_file_write_iter(iocb, from);
	inode_unlock(inode);

	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	return ret;
}
