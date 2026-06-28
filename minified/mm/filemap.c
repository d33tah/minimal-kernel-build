
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/sched/signal.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/uio.h>
/* error-injection.h - only need ALLOW_ERROR_INJECTION macro */
#define ALLOW_ERROR_INJECTION(fname, _etype)
#include <linux/hash.h>
#include <linux/pagevec.h>
#include <linux/memcontrol.h>
#include "internal.h"

#include <asm/mman.h>

noinline int __filemap_add_folio(struct address_space *mapping,
		struct folio *folio, pgoff_t index, gfp_t gfp, void **shadowp)
{
	XA_STATE(xas, &mapping->i_pages, index);
	long nr = 1;

	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);
	VM_BUG_ON_FOLIO(folio_test_swapbacked(folio), folio);
	mapping_set_update(&xas, mapping);

	{
		VM_BUG_ON_FOLIO(index & (folio_nr_pages(folio) - 1), folio);
		xas_set_order(&xas, index, folio_order(folio));
		nr = folio_nr_pages(folio);
	}

	gfp &= GFP_RECLAIM_MASK;
	folio_ref_add(folio, nr);
	folio->mapping = mapping;
	folio->index = xas.xa_index;

	do {
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
	
	folio_put_refs(folio, nr);
	return xas_error(&xas);
}
ALLOW_ERROR_INJECTION(__filemap_add_folio, ERRNO);

int filemap_add_folio(struct address_space *mapping, struct folio *folio,
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


#define PAGE_WAIT_TABLE_BITS 8
#define PAGE_WAIT_TABLE_SIZE (1 << PAGE_WAIT_TABLE_BITS)
static wait_queue_head_t folio_wait_table[PAGE_WAIT_TABLE_SIZE] __cacheline_aligned;

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

static int wake_page_function(wait_queue_entry_t *wait, unsigned mode, int sync, void *arg)
{
	unsigned int flags;
	struct wait_page_key *key = arg;
	struct wait_page_queue *wait_page
		= container_of(wait, struct wait_page_queue, wait);

	if (!wake_page_match(wait_page, key))
		return 0;

	
	flags = wait->flags;
	if (flags & WQ_FLAG_EXCLUSIVE) {
		if (test_bit(key->bit_nr, &key->folio->flags))
			return -1;
		if (flags & WQ_FLAG_CUSTOM) {
			if (test_and_set_bit(key->bit_nr, &key->folio->flags))
				return -1;
			flags |= WQ_FLAG_DONE;
		}
	}

	
	smp_store_release(&wait->flags, flags | WQ_FLAG_WOKEN);
	wake_up_state(wait->private, mode);

	
	list_del_init_careful(&wait->entry);
	return (flags & WQ_FLAG_EXCLUSIVE) != 0;
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

enum behavior {
	EXCLUSIVE,	
	SHARED,		
	DROP,		
};

static inline bool folio_trylock_flag(struct folio *folio, int bit_nr,
					struct wait_queue_entry *wait)
{
	if (wait->flags & WQ_FLAG_EXCLUSIVE) {
		if (test_and_set_bit(bit_nr, &folio->flags))
			return false;
	} else if (test_bit(bit_nr, &folio->flags))
		return false;

	wait->flags |= WQ_FLAG_WOKEN | WQ_FLAG_DONE;
	return true;
}

static inline int folio_wait_bit_common(struct folio *folio, int bit_nr,
		int state, enum behavior behavior)
{
	wait_queue_head_t *q = folio_waitqueue(folio);
	int unfairness = 5;
	struct wait_page_queue wait_page;
	wait_queue_entry_t *wait = &wait_page.wait;

	/* Stub: skip thrashing/delayacct tracking for minimal kernel */

	init_wait(wait);
	wait->func = wake_page_function;
	wait_page.folio = folio;
	wait_page.bit_nr = bit_nr;

repeat:
	wait->flags = 0;
	if (behavior == EXCLUSIVE) {
		wait->flags = WQ_FLAG_EXCLUSIVE;
		if (--unfairness < 0)
			wait->flags |= WQ_FLAG_CUSTOM;
	}

	spin_lock_irq(&q->lock);
	folio_set_waiters(folio);
	if (!folio_trylock_flag(folio, bit_nr, wait))
		__add_wait_queue_entry_tail(q, wait);
	spin_unlock_irq(&q->lock);

	if (behavior == DROP)
		folio_put(folio);

	for (;;) {
		unsigned int flags;

		set_current_state(state);

		flags = smp_load_acquire(&wait->flags);
		if (!(flags & WQ_FLAG_WOKEN)) {
			if (signal_pending_state(state, current))
				break;

			io_schedule();
			continue;
		}

		if (behavior != EXCLUSIVE)
			break;

		if (flags & WQ_FLAG_DONE)
			break;

		if (unlikely(test_and_set_bit(bit_nr, folio_flags(folio, 0))))
			goto repeat;

		wait->flags |= WQ_FLAG_DONE;
		break;
	}

	finish_wait(q, wait);

	/* Stub: skip thrashing/delayacct cleanup for minimal kernel */

	if (behavior == EXCLUSIVE)
		return wait->flags & WQ_FLAG_DONE ? 0 : -EINTR;

	return wait->flags & WQ_FLAG_WOKEN ? 0 : -EINTR;
}

int folio_wait_bit_killable(struct folio *folio, int bit_nr)
{
	return folio_wait_bit_common(folio, bit_nr, TASK_KILLABLE, SHARED);
}


#ifndef clear_bit_unlock_is_negative_byte

static inline bool clear_bit_unlock_is_negative_byte(long nr, volatile void *mem)
{
	clear_bit_unlock(nr, mem);
	
	return test_bit(PG_waiters, mem);
}

#endif

void folio_unlock(struct folio *folio)
{
	
	BUILD_BUG_ON(PG_waiters != 7);
	BUILD_BUG_ON(PG_locked > 7);
	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);
	if (clear_bit_unlock_is_negative_byte(PG_locked, folio_flags(folio, 0)))
		folio_wake_bit(folio, PG_locked);
}



void __folio_lock(struct folio *folio)
{
	folio_wait_bit_common(folio, PG_locked, TASK_UNINTERRUPTIBLE,
				EXCLUSIVE);
}

static void *mapping_get_entry(struct address_space *mapping, pgoff_t index)
{
	XA_STATE(xas, &mapping->i_pages, index);
	struct folio *folio;

	rcu_read_lock();
repeat:
	xas_reset(&xas);
	folio = xas_load(&xas);
	if (xas_retry(&xas, folio))
		goto repeat;
	
	if (!folio || xa_is_value(folio))
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
	if (xa_is_value(folio)) {
		if (fgp_flags & FGP_ENTRY)
			return folio;
		folio = NULL;
	}
	if (!folio)
		goto no_page;

	if (fgp_flags & FGP_LOCK) {
		if (fgp_flags & FGP_NOWAIT) {
			if (!folio_trylock(folio)) {
				folio_put(folio);
				return NULL;
			}
		} else {
			folio_lock(folio);
		}

		
		if (unlikely(folio->mapping != mapping)) {
			folio_unlock(folio);
			folio_put(folio);
			goto repeat;
		}
		VM_BUG_ON_FOLIO(!folio_contains(folio, index), folio);
	}

	if (fgp_flags & FGP_ACCESSED)
		folio_mark_accessed(folio);

no_page:
	if (!folio && (fgp_flags & FGP_CREAT)) {
		int err;
		if (fgp_flags & FGP_NOFS)
			gfp &= ~__GFP_FS;

		folio = filemap_alloc_folio(gfp, 0);
		if (!folio)
			return NULL;

		if (WARN_ON_ONCE(!(fgp_flags & (FGP_LOCK | FGP_FOR_MMAP))))
			fgp_flags |= FGP_LOCK;

		
		if (fgp_flags & FGP_ACCESSED)
			__folio_set_referenced(folio);

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

static inline
bool folio_more_pages(struct folio *folio, pgoff_t index, pgoff_t max)
{
	if (!folio_test_large(folio))
		return false;
	if (index >= max)
		return false;
	return index < folio->index + folio_nr_pages(folio) - 1;
}


static void filemap_get_read_batch(struct address_space *mapping,
		pgoff_t index, pgoff_t max, struct folio_batch *fbatch)
{
	XA_STATE(xas, &mapping->i_pages, index);
	struct folio *folio;

	rcu_read_lock();
	for (folio = xas_load(&xas); folio; folio = xas_next(&xas)) {
		if (xas_retry(&xas, folio))
			continue;
		if (xas.xa_index > max || xa_is_value(folio))
			break;
		if (xa_is_sibling(folio))
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

static bool filemap_range_uptodate(struct address_space *mapping,
		loff_t pos, struct iov_iter *iter, struct folio *folio)
{
	/*
	 * No address_space_operations on this build sets
	 * ->is_partially_uptodate (ram_aops only sets read_folio/write_begin/
	 * write_end), so a folio that isn't fully uptodate is never partially
	 * uptodate here.
	 */
	return folio_test_uptodate(folio);
}

static int filemap_update_page(struct kiocb *iocb,
		struct address_space *mapping, struct iov_iter *iter,
		struct folio *folio)
{
	int error;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!filemap_invalidate_trylock_shared(mapping))
			return -EAGAIN;
	} else {
		filemap_invalidate_lock_shared(mapping);
	}

	if (!folio_trylock(folio)) {
		error = -EAGAIN;
		if (iocb->ki_flags & (IOCB_NOWAIT | IOCB_NOIO))
			goto unlock_mapping;
		/*
		 * IOCB_WAITQ (io_uring async) is never set in this tree, so the
		 * async __folio_lock path is unreachable; always wait sync.
		 */
		filemap_invalidate_unlock_shared(mapping);
		return AOP_TRUNCATED_PAGE;
	}

	error = AOP_TRUNCATED_PAGE;
	if (!folio->mapping)
		goto unlock;

	error = 0;
	if (filemap_range_uptodate(mapping, iocb->ki_pos, iter, folio))
		goto unlock;

	error = -EAGAIN;
	if (iocb->ki_flags & (IOCB_NOIO | IOCB_NOWAIT | IOCB_WAITQ))
		goto unlock;

	error = filemap_read_folio(iocb->ki_filp, mapping, folio);
	goto unlock_mapping;
unlock:
	folio_unlock(folio);
unlock_mapping:
	filemap_invalidate_unlock_shared(mapping);
	if (error == AOP_TRUNCATED_PAGE)
		folio_put(folio);
	return error;
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
	struct file_ra_state *ra = &filp->f_ra;
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
		if (iocb->ki_flags & IOCB_NOIO)
			return -EAGAIN;
		page_cache_sync_readahead(mapping, ra, filp, index,
				last_index - index);
		filemap_get_read_batch(mapping, index, last_index, fbatch);
	}
	if (!folio_batch_count(fbatch)) {
		if (iocb->ki_flags & (IOCB_NOWAIT | IOCB_WAITQ))
			return -EAGAIN;
		err = filemap_create_folio(filp, mapping,
				iocb->ki_pos >> PAGE_SHIFT, fbatch);
		if (err == AOP_TRUNCATED_PAGE)
			goto retry;
		return err;
	}

	folio = fbatch->folios[folio_batch_count(fbatch) - 1];
	if (!folio_test_uptodate(folio)) {
		if ((iocb->ki_flags & IOCB_WAITQ) &&
		    folio_batch_count(fbatch) > 1)
			iocb->ki_flags |= IOCB_NOWAIT;
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

static inline bool pos_same_folio(loff_t pos1, loff_t pos2, struct folio *folio)
{
	unsigned int shift = folio_shift(folio);

	return (pos1 >> shift == pos2 >> shift);
}

ssize_t filemap_read(struct kiocb *iocb, struct iov_iter *iter,
		ssize_t already_read)
{
	struct file *filp = iocb->ki_filp;
	struct file_ra_state *ra = &filp->f_ra;
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = mapping->host;
	struct folio_batch fbatch;
	int i, error = 0;
	bool writably_mapped;
	loff_t isize, end_offset;

	if (unlikely(iocb->ki_pos >= inode->i_sb->s_maxbytes))
		return 0;
	if (unlikely(!iov_iter_count(iter)))
		return 0;

	iov_iter_truncate(iter, inode->i_sb->s_maxbytes);
	folio_batch_init(&fbatch);

	do {
		cond_resched();

		
		if ((iocb->ki_flags & IOCB_WAITQ) && already_read)
			iocb->ki_flags |= IOCB_NOWAIT;

		if (unlikely(iocb->ki_pos >= i_size_read(inode)))
			break;

		error = filemap_get_pages(iocb, iter, &fbatch);
		if (error < 0)
			break;

		
		isize = i_size_read(inode);
		if (unlikely(iocb->ki_pos >= isize))
			goto put_folios;
		end_offset = min_t(loff_t, isize, iocb->ki_pos + iter->count);

		
		writably_mapped = mapping_writably_mapped(mapping);

		
		if (!pos_same_folio(iocb->ki_pos, ra->prev_pos - 1,
							fbatch.folios[0]))
			folio_mark_accessed(fbatch.folios[0]);

		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];
			size_t fsize = folio_size(folio);
			size_t offset = iocb->ki_pos & (fsize - 1);
			size_t bytes = min_t(loff_t, end_offset - iocb->ki_pos,
					     fsize - offset);
			size_t copied;

			if (end_offset < folio_pos(folio))
				break;
			if (i > 0)
				folio_mark_accessed(folio);
			
			if (writably_mapped)
				flush_dcache_folio(folio);

			copied = copy_folio_to_iter(folio, offset, bytes, iter);

			already_read += copied;
			iocb->ki_pos += copied;
			ra->prev_pos = iocb->ki_pos;

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

	file_accessed(filp);

	return already_read ? already_read : error;
}

ssize_t
generic_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	size_t count = iov_iter_count(iter);
	ssize_t retval = 0;

	if (!count)
		return 0;

	return filemap_read(iocb, iter, retval);
}


#define MMAP_LOTSAMISS  (100)

vm_fault_t filemap_fault(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	pgoff_t index = vmf->pgoff;
	struct folio *folio;

	folio = __filemap_get_folio(mapping, index, FGP_CREAT|FGP_FOR_MMAP, vmf->gfp_mask);
	if (!folio)
		return VM_FAULT_OOM;

	if (!folio_test_locked(folio))
		folio_lock(folio);

	vmf->page = folio_file_page(folio, index);
	return VM_FAULT_LOCKED;
}

static bool filemap_map_pmd(struct vm_fault *vmf, struct page *page)
{
	struct mm_struct *mm = vmf->vma->vm_mm;

	if (pmd_none(*vmf->pmd))
		pmd_install(mm, vmf->pmd, &vmf->prealloc_pte);

	return false;
}

static struct folio *next_uptodate_page(struct folio *folio,
				       struct address_space *mapping,
				       struct xa_state *xas, pgoff_t end_pgoff)
{
	unsigned long max_idx;

	do {
		if (!folio)
			return NULL;
		if (xas_retry(xas, folio))
			continue;
		if (xa_is_value(folio))
			continue;
		if (folio_test_locked(folio))
			continue;
		if (!folio_try_get_rcu(folio))
			continue;
		
		if (unlikely(folio != xas_reload(xas)))
			goto skip;
		if (!folio_test_uptodate(folio) || folio_test_readahead(folio))
			goto skip;
		if (!folio_trylock(folio))
			goto skip;
		if (folio->mapping != mapping)
			goto unlock;
		if (!folio_test_uptodate(folio))
			goto unlock;
		max_idx = DIV_ROUND_UP(i_size_read(mapping->host), PAGE_SIZE);
		if (xas->xa_index >= max_idx)
			goto unlock;
		return folio;
unlock:
		folio_unlock(folio);
skip:
		folio_put(folio);
	} while ((folio = xas_next_entry(xas, end_pgoff)) != NULL);

	return NULL;
}

static inline struct folio *first_map_page(struct address_space *mapping,
					  struct xa_state *xas,
					  pgoff_t end_pgoff)
{
	return next_uptodate_page(xas_find(xas, end_pgoff),
				  mapping, xas, end_pgoff);
}

static inline struct folio *next_map_page(struct address_space *mapping,
					 struct xa_state *xas,
					 pgoff_t end_pgoff)
{
	return next_uptodate_page(xas_next_entry(xas, end_pgoff),
				  mapping, xas, end_pgoff);
}

vm_fault_t filemap_map_pages(struct vm_fault *vmf,
			     pgoff_t start_pgoff, pgoff_t end_pgoff)
{
	struct vm_area_struct *vma = vmf->vma;
	struct file *file = vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	pgoff_t last_pgoff = start_pgoff;
	unsigned long addr;
	XA_STATE(xas, &mapping->i_pages, start_pgoff);
	struct folio *folio;
	struct page *page;
	unsigned int mmap_miss = READ_ONCE(file->f_ra.mmap_miss);
	vm_fault_t ret = 0;

	rcu_read_lock();
	folio = first_map_page(mapping, &xas, end_pgoff);
	if (!folio)
		goto out;

	if (filemap_map_pmd(vmf, &folio->page)) {
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

	addr = vma->vm_start + ((start_pgoff - vma->vm_pgoff) << PAGE_SHIFT);
	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, addr, &vmf->ptl);
	do {
again:
		page = folio_file_page(folio, xas.xa_index);

		if (mmap_miss > 0)
			mmap_miss--;

		addr += (xas.xa_index - last_pgoff) << PAGE_SHIFT;
		vmf->pte += xas.xa_index - last_pgoff;
		last_pgoff = xas.xa_index;

		
		if (!pte_none(*vmf->pte))
			goto unlock;

		
		if (vmf->address == addr)
			ret = VM_FAULT_NOPAGE;

		do_set_pte(vmf, page, addr);
		if (folio_more_pages(folio, xas.xa_index, end_pgoff)) {
			xas.xa_index++;
			folio_ref_inc(folio);
			goto again;
		}
		folio_unlock(folio);
		continue;
unlock:
		if (folio_more_pages(folio, xas.xa_index, end_pgoff)) {
			xas.xa_index++;
			goto again;
		}
		folio_unlock(folio);
		folio_put(folio);
	} while ((folio = next_map_page(mapping, &xas, end_pgoff)) != NULL);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
out:
	rcu_read_unlock();
	WRITE_ONCE(file->f_ra.mmap_miss, mmap_miss);
	return ret;
}

vm_fault_t filemap_page_mkwrite(struct vm_fault *vmf)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	struct folio *folio = page_folio(vmf->page);
	vm_fault_t ret = VM_FAULT_LOCKED;

	sb_start_pagefault(mapping->host->i_sb);
	file_update_time(vmf->vma->vm_file);
	folio_lock(folio);
	if (folio->mapping != mapping) {
		folio_unlock(folio);
		ret = VM_FAULT_NOPAGE;
		goto out;
	}
	
	folio_mark_dirty(folio);
out:
	sb_end_pagefault(mapping->host->i_sb);
	return ret;
}

const struct vm_operations_struct generic_file_vm_ops = {
	.fault		= filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= filemap_page_mkwrite,
};

int generic_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct address_space *mapping = file->f_mapping;

	if (!mapping->a_ops->read_folio)
		return -ENOEXEC;
	file_accessed(file);
	vma->vm_ops = &generic_file_vm_ops;
	return 0;
}

ssize_t generic_perform_write(struct kiocb *iocb, struct iov_iter *i)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	const struct address_space_operations *a_ops = mapping->a_ops;
	struct page *page;
	void *fsdata;
	ssize_t written = 0;
	size_t bytes = iov_iter_count(i);

	if (a_ops->write_begin(file, mapping, iocb->ki_pos, bytes, &page, &fsdata) < 0)
		return -EIO;
	written = copy_page_from_iter_atomic(page, 0, bytes, i);
	a_ops->write_end(file, mapping, iocb->ki_pos, bytes, written, page, fsdata);
	return written;
}

ssize_t __generic_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	ssize_t		written = 0;
	ssize_t		err;


	err = file_remove_privs(file);
	if (err)
		goto out;

	err = file_update_time(file);
	if (err)
		goto out;

	written = generic_perform_write(iocb, from);
	if (likely(written > 0))
		iocb->ki_pos += written;
out:
	return written ? written : err;
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

