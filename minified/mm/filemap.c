
#include <linux/fs.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
/* capability.h removed - unused */
/* kernel_stat.h removed - unused */
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/uio.h>
/* error-injection.h removed - unused */
#include <linux/hash.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>
/* cpuset.h removed - unused */
/* linux/hugetlb.h removed - folio_test_hugetlb always false */
/* memcontrol.h removed - unused */
/* rmap.h, ramfs.h, migrate.h removed - unused */
#include "internal.h"

#include <asm/mman.h>

/* page_cache_delete inlined into __filemap_remove_folio */

static void filemap_unaccount_folio(struct address_space *mapping,
				    struct folio *folio)
{
	long nr;

	/* DEBUG_VM disabled - always check */
	if (unlikely(folio_mapped(folio))) {
		pr_alert("BUG: Bad page cache in process %s  pfn:%05lx\n",
			 current->comm,
			 page_to_pfn(&folio->page)); /* folio_pfn inlined */
		add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);

		/* mapping_exiting inlined - single caller */
		if (test_bit(AS_EXITING, &mapping->flags) &&
		    !folio_test_large(folio)) {
			/* page_mapcount inlined - single caller */
			struct page *pg = &folio->page;
			int mapcount =
				unlikely(PageCompound(pg)) ?
					__page_mapcount(pg) :
					(atomic_read(&pg->_mapcount) + 1);

			if (folio_ref_count(folio) >= mapcount + 2) {
				page_mapcount_reset(&folio->page);
				atomic_sub(mapcount, &folio->page._refcount);
			}
		}
	}

	/* folio_test_hugetlb always returns false, skip check */

	nr = folio_nr_pages(folio);

	__lruvec_stat_mod_folio(folio, NR_FILE_PAGES, -nr);
	if (folio_test_swapbacked(folio)) {
		__lruvec_stat_mod_folio(folio, NR_SHMEM, -nr);
		/* folio_test_pmd_mappable always returns false */
	}
	/* else if folio_test_pmd_mappable block removed - always false */

	/* folio_account_cleaned call removed - was empty stub */
}

void __filemap_remove_folio(struct folio *folio, void *shadow)
{
	struct address_space *mapping = folio->mapping;

	filemap_unaccount_folio(mapping, folio);
	/* Inlined page_cache_delete */
	{
		XA_STATE(xas, &mapping->i_pages, folio->index);
		long nr;

		mapping_set_update(&xas, mapping);
		xas_set_order(&xas, folio->index, folio_order(folio));
		nr = folio_nr_pages(folio);

		xas_store(&xas, shadow);
		xas_init_marks(&xas);

		folio->mapping = NULL;
		mapping->nrpages -= nr;
	}
}

void filemap_free_folio(struct address_space *mapping, struct folio *folio)
{
	int refs = 1;

	/* a_ops->free_folio check removed - never set */

	/* folio_test_hugetlb always false, simplify */
	if (folio_test_large(folio))
		refs = folio_nr_pages(folio);
	folio_put_refs(folio, refs);
}

void filemap_remove_folio(struct folio *folio)
{
	struct address_space *mapping = folio->mapping;

	BUG_ON(!folio_test_locked(folio));
	spin_lock(&mapping->host->i_lock);
	xa_lock_irq(&mapping->i_pages);
	__filemap_remove_folio(folio, NULL);
	xa_unlock_irq(&mapping->i_pages);
	if (mapping_shrinkable(mapping))
		inode_add_lru(mapping->host);
	spin_unlock(&mapping->host->i_lock);

	filemap_free_folio(mapping, folio);
}

void delete_from_page_cache_batch(struct address_space *mapping,
				  struct folio_batch *fbatch)
{
	XA_STATE(xas, &mapping->i_pages, fbatch->folios[0]->index);
	long total_pages = 0;
	int i = 0;
	struct folio *folio;

	if (!folio_batch_count(fbatch))
		return;

	spin_lock(&mapping->host->i_lock);
	xa_lock_irq(&mapping->i_pages);
	for (i = 0; i < folio_batch_count(fbatch); i++) {
		folio = fbatch->folios[i];
		filemap_unaccount_folio(mapping, folio);
	}
	/* Inline page_cache_delete_batch */
	i = 0;
	mapping_set_update(&xas, mapping);
	xas_for_each(&xas, folio, ULONG_MAX) {
		if (i >= folio_batch_count(fbatch))
			break;
		if (xa_is_value(folio))
			continue;
		if (folio != fbatch->folios[i])
			continue;
		WARN_ON_ONCE(!folio_test_locked(folio));
		folio->mapping = NULL;
		i++;
		xas_store(&xas, NULL);
		total_pages += folio_nr_pages(folio);
	}
	mapping->nrpages -= total_pages;
	xa_unlock_irq(&mapping->i_pages);
	if (mapping_shrinkable(mapping))
		inode_add_lru(mapping->host);
	spin_unlock(&mapping->host->i_lock);

	for (i = 0; i < folio_batch_count(fbatch); i++)
		filemap_free_folio(mapping, fbatch->folios[i]);
}

/* filemap_check_errors, filemap_fdatawrite_wbc, filemap_fdatawait_range removed - never called */
/* filemap_write_and_wait_range, file_check_and_advance_wb_err, file_write_and_wait_range removed - never called */
noinline int __filemap_add_folio(struct address_space *mapping,
				 struct folio *folio, pgoff_t index, gfp_t gfp,
				 void **shadowp)
{
	XA_STATE(xas, &mapping->i_pages, index);
	/* folio_test_hugetlb always false, simplify */
	long nr;

	mapping_set_update(&xas, mapping);

	/* mem_cgroup_charge always returns 0 */
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

		mapping->nrpages += nr;

		/* huge (folio_test_hugetlb) is always false */
		__lruvec_stat_mod_folio(folio, NR_FILE_PAGES, nr);
unlock:
		xas_unlock_irq(&xas);
	} while (xas_nomem(&xas, gfp));

	if (xas_error(&xas))
		goto error;

	return 0;
error:
	/* mem_cgroup_uncharge is empty stub */
	folio->mapping = NULL;

	folio_put_refs(folio, nr);
	return xas_error(&xas);
}

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
		/* workingset_refault removed - was empty stub */
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
	/* page_writeback_init removed - empty stub */
}

static int wake_page_function(wait_queue_entry_t *wait, unsigned mode, int sync,
			      void *arg)
{
	unsigned int flags;
	struct wait_page_key *key = arg;
	struct wait_page_queue *wait_page =
		container_of(wait, struct wait_page_queue, wait);

	/* wake_page_match inlined - single caller */
	if (wait_page->folio != key->folio)
		return 0;
	key->page_match = 1;
	if (wait_page->bit_nr != key->bit_nr)
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

/* folio_trylock_flag inlined into folio_wait_bit_common */

int sysctl_page_lock_unfairness = 5;

static inline int folio_wait_bit_common(struct folio *folio, int bit_nr,
					int state, enum behavior behavior)
{
	wait_queue_head_t *q = folio_waitqueue(folio);
	int unfairness = sysctl_page_lock_unfairness;
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
	/* folio_trylock_flag inlined */
	{
		bool locked;
		if (wait->flags & WQ_FLAG_EXCLUSIVE)
			locked = !test_and_set_bit(bit_nr, &folio->flags);
		else
			locked = !test_bit(bit_nr, &folio->flags);
		if (locked)
			wait->flags |= WQ_FLAG_WOKEN | WQ_FLAG_DONE;
		else
			__add_wait_queue_entry_tail(q, wait);
	}
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

void folio_wait_bit(struct folio *folio, int bit_nr)
{
	folio_wait_bit_common(folio, bit_nr, TASK_UNINTERRUPTIBLE, SHARED);
}

int folio_wait_bit_killable(struct folio *folio, int bit_nr)
{
	return folio_wait_bit_common(folio, bit_nr, TASK_KILLABLE, SHARED);
}

/* folio_put_wait_locked removed - always returns 0 */

void folio_unlock(struct folio *folio)
{
	BUILD_BUG_ON(PG_waiters != 7);
	BUILD_BUG_ON(PG_locked > 7);
	/* inlined clear_bit_unlock_is_negative_byte */
	clear_bit_unlock(PG_locked, folio_flags(folio, 0));
	if (test_bit(PG_waiters, folio_flags(folio, 0)))
		folio_wake_bit(folio, PG_locked);
}

/* folio_end_writeback removed - never called */

void __folio_lock(struct folio *folio)
{
	folio_wait_bit_common(folio, PG_locked, TASK_UNINTERRUPTIBLE,
			      EXCLUSIVE);
}

int __folio_lock_killable(struct folio *folio)
{
	return folio_wait_bit_common(folio, PG_locked, TASK_KILLABLE,
				     EXCLUSIVE);
}

/* __folio_lock_async inlined into filemap_update_page (~20 LOC) */

bool __folio_lock_or_retry(struct folio *folio, struct mm_struct *mm,
			   unsigned int flags)
{
	/* fault_flag_allow_retry_first inlined */
	if ((flags & FAULT_FLAG_ALLOW_RETRY) && !(flags & FAULT_FLAG_TRIED)) {
		/* FAULT_FLAG_RETRY_NOWAIT check removed - never set */
		mmap_read_unlock(mm);
		if (flags & FAULT_FLAG_KILLABLE)
			folio_wait_locked_killable(folio);
		else
			folio_wait_locked(folio);
		return false;
	}
	if (flags & FAULT_FLAG_KILLABLE) {
		bool ret;

		ret = __folio_lock_killable(folio);
		if (ret) {
			mmap_read_unlock(mm);
			return false;
		}
	} else {
		__folio_lock(folio);
	}

	return true;
}

/* mapping_get_entry inlined into __filemap_get_folio */

struct folio *__filemap_get_folio(struct address_space *mapping, pgoff_t index,
				  int fgp_flags, gfp_t gfp)
{
	XA_STATE(xas, &mapping->i_pages, index);
	struct folio *folio;

repeat:
	/* mapping_get_entry inlined */
	rcu_read_lock();
repeat_xas:
	xas_reset(&xas);
	folio = xas_load(&xas);
	if (xas_retry(&xas, folio))
		goto repeat_xas;
	if (!folio || xa_is_value(folio))
		goto out_rcu;
	if (!folio_try_get_rcu(folio))
		goto repeat_xas;
	if (unlikely(folio != xas_reload(&xas))) {
		folio_put(folio);
		goto repeat_xas;
	}
out_rcu:
	rcu_read_unlock();
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
	}

	if (fgp_flags & FGP_ACCESSED)
		folio_mark_accessed(folio);

	/* FGP_STABLE / folio_wait_stable removed - was empty stub */
no_page:
	if (!folio && (fgp_flags & FGP_CREAT)) {
		int err;
		if ((fgp_flags & FGP_WRITE) && mapping_can_writeback(mapping))
			gfp |= __GFP_WRITE;
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

static inline struct folio *find_get_entry(struct xa_state *xas, pgoff_t max,
					   xa_mark_t mark)
{
	struct folio *folio;

retry:
	if (mark == XA_PRESENT)
		folio = xas_find(xas, max);
	else
		folio = xas_find_marked(xas, max, mark);

	if (xas_retry(xas, folio))
		goto retry;

	if (!folio || xa_is_value(folio))
		return folio;

	if (!folio_try_get_rcu(folio))
		goto reset;

	if (unlikely(folio != xas_reload(xas))) {
		folio_put(folio);
		goto reset;
	}

	return folio;
reset:
	xas_reset(xas);
	goto retry;
}

unsigned find_get_entries(struct address_space *mapping, pgoff_t start,
			  pgoff_t end, struct folio_batch *fbatch,
			  pgoff_t *indices)
{
	XA_STATE(xas, &mapping->i_pages, start);
	struct folio *folio;

	rcu_read_lock();
	while ((folio = find_get_entry(&xas, end, XA_PRESENT)) != NULL) {
		indices[fbatch->nr] = xas.xa_index;
		if (!folio_batch_add(fbatch, folio))
			break;
	}
	rcu_read_unlock();

	return folio_batch_count(fbatch);
}

unsigned find_lock_entries(struct address_space *mapping, pgoff_t start,
			   pgoff_t end, struct folio_batch *fbatch,
			   pgoff_t *indices)
{
	XA_STATE(xas, &mapping->i_pages, start);
	struct folio *folio;

	rcu_read_lock();
	while ((folio = find_get_entry(&xas, end, XA_PRESENT))) {
		if (!xa_is_value(folio)) {
			if (folio->index < start)
				goto put;
			if (folio->index + folio_nr_pages(folio) - 1 > end)
				goto put;
			if (!folio_trylock(folio))
				goto put;
			if (folio->mapping != mapping ||
			    folio_test_writeback(folio))
				goto unlock;
		}
		indices[fbatch->nr] = xas.xa_index;
		if (!folio_batch_add(fbatch, folio))
			break;
		continue;
unlock:
		folio_unlock(folio);
put:
		folio_put(folio);
	}
	rcu_read_unlock();

	return folio_batch_count(fbatch);
}

/* folio_more_pages removed - orphaned after filemap_map_pages removal (~10 LOC) */

unsigned find_get_pages_range_tag(struct address_space *mapping, pgoff_t *index,
				  pgoff_t end, xa_mark_t tag,
				  unsigned int nr_pages, struct page **pages)
{
	XA_STATE(xas, &mapping->i_pages, *index);
	struct folio *folio;
	unsigned ret = 0;

	if (unlikely(!nr_pages))
		return 0;

	rcu_read_lock();
	while ((folio = find_get_entry(&xas, end, tag))) {
		if (xa_is_value(folio))
			continue;

		pages[ret] = &folio->page;
		if (++ret == nr_pages) {
			*index = folio->index + folio_nr_pages(folio);
			goto out;
		}
	}

	if (end == (pgoff_t)-1)
		*index = (pgoff_t)-1;
	else
		*index = end + 1;
out:
	rcu_read_unlock();

	return ret;
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
		/* xa_is_sibling always returns false - check removed */
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

/* filemap_range_uptodate inlined - is_partially_uptodate never set */

static int filemap_update_page(struct kiocb *iocb,
			       struct address_space *mapping,
			       struct iov_iter *iter, struct folio *folio)
{
	int error;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!down_read_trylock(&mapping->invalidate_lock))
			return -EAGAIN;
	} else {
		filemap_invalidate_lock_shared(mapping);
	}

	if (!folio_trylock(folio)) {
		error = -EAGAIN;
		if (iocb->ki_flags & (IOCB_NOWAIT | IOCB_NOIO))
			goto unlock_mapping;
		if (!(iocb->ki_flags & IOCB_WAITQ)) {
			filemap_invalidate_unlock_shared(mapping);
			/* folio_put_wait_locked removed - returns 0 */
			return AOP_TRUNCATED_PAGE;
		}
		/* inlined __folio_lock_async */
		{
			struct wait_page_queue *wait = iocb->ki_waitq;
			struct wait_queue_head *q = folio_waitqueue(folio);
			wait->folio = folio;
			wait->bit_nr = PG_locked;
			spin_lock_irq(&q->lock);
			__add_wait_queue_entry_tail(q, &wait->wait);
			folio_set_waiters(folio);
			error = !folio_trylock(folio);
			if (!error)
				__remove_wait_queue(q, &wait->wait);
			else
				error = -EIOCBQUEUED;
			spin_unlock_irq(&q->lock);
			if (error)
				goto unlock_mapping;
		}
	}

	error = AOP_TRUNCATED_PAGE;
	if (!folio->mapping)
		goto unlock;

	error = 0;
	if (folio_test_uptodate(folio))
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

/* filemap_create_folio inlined into filemap_get_pages */

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
		if (iocb->ki_flags & IOCB_NOIO)
			return -EAGAIN;
		filemap_get_read_batch(mapping, index, last_index, fbatch);
	}
	if (!folio_batch_count(fbatch)) {
		if (iocb->ki_flags & (IOCB_NOWAIT | IOCB_WAITQ))
			return -EAGAIN;
		/* filemap_create_folio inlined */
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
	/* Removed: filemap_readahead - stub always returned 0 */
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

/* pos_same_folio inlined into filemap_read */

ssize_t filemap_read(struct kiocb *iocb, struct iov_iter *iter,
		     ssize_t already_read)
{
	struct file *filp = iocb->ki_filp;
	struct file_ra_state *ra = &filp->f_ra;
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

		/* pos_same_folio inlined, folio_shift inlined */
		{
			unsigned int shift =
				PAGE_SHIFT + folio_order(fbatch.folios[0]);
			if (!(iocb->ki_pos >> shift ==
			      (ra->prev_pos - 1) >> shift))
				folio_mark_accessed(fbatch.folios[0]);
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
			if (i > 0)
				folio_mark_accessed(folio);

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

	/* file_accessed removed - touch_atime is empty stub */

	return already_read ? already_read : error;
}

ssize_t generic_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	size_t count = iov_iter_count(iter);

	if (!count)
		return 0;

	/* IOCB_DIRECT block removed - a_ops->direct_IO is never set (~30 LOC) */

	return filemap_read(iocb, iter, 0);
}
/* MMAP_LOTSAMISS removed - unused */

vm_fault_t filemap_fault(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	pgoff_t index = vmf->pgoff;
	struct folio *folio;

	folio = __filemap_get_folio(mapping, index, FGP_CREAT | FGP_FOR_MMAP,
				    vmf->gfp_mask);
	if (!folio)
		return VM_FAULT_OOM;

	if (!folio_test_locked(folio))
		folio_lock(folio);

	vmf->page = folio_file_page(folio, index);
	return VM_FAULT_LOCKED;
}

/* next_uptodate_page and filemap_map_pages removed (~108 LOC) */

vm_fault_t filemap_page_mkwrite(struct vm_fault *vmf)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	struct folio *folio = page_folio(vmf->page);
	vm_fault_t ret = VM_FAULT_LOCKED;

	sb_start_pagefault(mapping->host->i_sb);
	/* file_update_time removed - stub returns 0 */
	folio_lock(folio);
	if (folio->mapping != mapping) {
		folio_unlock(folio);
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

	folio_mark_dirty(folio);
	/* folio_wait_stable removed - was empty stub */
out:
	sb_end_pagefault(mapping->host->i_sb);
	return ret;
}

const struct vm_operations_struct generic_file_vm_ops = {
	.fault = filemap_fault,
	.page_mkwrite = filemap_page_mkwrite,
};

int generic_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct address_space *mapping = file->f_mapping;

	if (!mapping->a_ops->read_folio)
		return -ENOEXEC;
	/* file_accessed removed - touch_atime is empty stub */
	vma->vm_ops = &generic_file_vm_ops;
	return 0;
}

/* do_read_cache_folio, read_cache_folio, do_read_cache_page, read_cache_page, read_cache_page_gfp, generic_file_direct_write removed - never called */

/* Write operations stubbed - minimal kernel is read-only during boot */
ssize_t generic_perform_write(struct kiocb *iocb, struct iov_iter *i)
{
	return -EROFS;
}

ssize_t __generic_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	return -EROFS;
}

ssize_t generic_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	return -EROFS;
}

/* filemap_release_folio removed - never called */

/* Merged from folio-compat.c */
void unlock_page(struct page *page)
{
	return folio_unlock(page_folio(page));
}

bool set_page_dirty(struct page *page)
{
	return folio_mark_dirty(page_folio(page));
}

noinline struct page *pagecache_get_page(struct address_space *mapping,
					 pgoff_t index, int fgp_flags,
					 gfp_t gfp)
{
	struct folio *folio;

	folio = __filemap_get_folio(mapping, index, fgp_flags, gfp);
	if ((fgp_flags & FGP_HEAD) || !folio || xa_is_value(folio))
		return &folio->page;
	return folio_file_page(folio, index);
}
