#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <linux/uaccess.h>
#include <linux/gfp.h>
#include <linux/bitops.h>
#include <linux/hardirq.h>

/* is_vm_hugetlb_page removed - callers removed */

struct folio_batch;

/* invalidate_mapping_pages, invalidate_inode_pages2_range removed - never called */
/* write_inode_now removed - returns 0 stub, caller simplified */
int filemap_fdatawait_range(struct address_space *, loff_t lstart, loff_t lend);
/* filemap_write_and_wait_range, filemap_fdatawrite_wbc, filemap_sample_wb_err removed - never called */
static inline bool mapping_empty(struct address_space *mapping)
{
	return xa_empty(&mapping->i_pages);
}

static inline bool mapping_shrinkable(struct address_space *mapping)
{
	void *head;

	head = rcu_access_pointer(mapping->i_pages.xa_head);
	if (!head)
		return true;

	if (!xa_is_node(head) && xa_is_value(head))
		return true;

	return false;
}

enum mapping_flags {
	AS_EIO		= 0,
	AS_ENOSPC	= 1,
	AS_UNEVICTABLE	= 3,
	AS_EXITING	= 4,
};

/* mapping_set_unevictable inlined at fs/ramfs/inode.c - single caller */
/* mapping_unevictable inlined at mm/swap.c - single caller */
/* mapping_set_exiting inlined at mm/truncate.c - single caller */
/* mapping_exiting inlined at mm/filemap.c - single caller */

static inline gfp_t mapping_gfp_mask(struct address_space * mapping)
{
	return mapping->gfp_mask;
}

static inline gfp_t mapping_gfp_constraint(struct address_space *mapping,
		gfp_t gfp_mask)
{
	return mapping_gfp_mask(mapping) & gfp_mask;
}

static inline void mapping_set_gfp_mask(struct address_space *m, gfp_t mask)
{
	m->gfp_mask = mask;
}


/* filemap_nr_thps removed - never called */

void release_pages(struct page **pages, int nr);

/* page_mapping removed - never called */
struct address_space *folio_mapping(struct folio *);


static inline struct folio *filemap_alloc_folio(gfp_t gfp, unsigned int order)
{
	return folio_alloc(gfp, order);
}

#define FGP_ACCESSED		0x00000001
#define FGP_LOCK		0x00000002
#define FGP_CREAT		0x00000004
#define FGP_WRITE		0x00000008
#define FGP_NOFS		0x00000010
#define FGP_NOWAIT		0x00000020
#define FGP_FOR_MMAP		0x00000040
#define FGP_HEAD		0x00000080
#define FGP_ENTRY		0x00000100
#define FGP_STABLE		0x00000200

struct folio *__filemap_get_folio(struct address_space *mapping, pgoff_t index,
		int fgp_flags, gfp_t gfp);
struct page *pagecache_get_page(struct address_space *mapping, pgoff_t index,
		int fgp_flags, gfp_t gfp);

/* filemap_get_folio, find_get_page, find_lock_page removed - never called/inlined */

/* folio_index removed - inlined at single call site */

static inline struct page *folio_file_page(struct folio *folio, pgoff_t index)
{
	/* folio_test_hugetlb always false, simplify */
	return folio_page(folio, index & (folio_nr_pages(folio) - 1));
}

/* folio_contains removed - never called */

unsigned find_get_pages_range_tag(struct address_space *mapping, pgoff_t *index,
			pgoff_t end, xa_mark_t tag, unsigned int nr_pages,
			struct page **pages);
/* grab_cache_page_write_begin, read_cache_folio, read_cache_page, read_mapping_page removed - never called */

/* page_to_pgoff removed - never called */

static inline loff_t page_offset(struct page *page)
{
	return ((loff_t)page->index) << PAGE_SHIFT;
}

static inline loff_t folio_pos(struct folio *folio)
{
	return page_offset(&folio->page);
}

/* linear_hugepage_index removed - declared but never called */

static inline pgoff_t linear_page_index(struct vm_area_struct *vma,
					unsigned long address)
{
	pgoff_t pgoff;
	/* is_vm_hugetlb_page always returns false */
	pgoff = (address - vma->vm_start) >> PAGE_SHIFT;
	pgoff += vma->vm_pgoff;
	return pgoff;
}

struct wait_page_key {
	struct folio *folio;
	int bit_nr;
	int page_match;
};

struct wait_page_queue {
	struct folio *folio;
	int bit_nr;
	wait_queue_entry_t wait;
};

/* wake_page_match inlined at mm/filemap.c - single caller */

void __folio_lock(struct folio *folio);
int __folio_lock_killable(struct folio *folio);
bool __folio_lock_or_retry(struct folio *folio, struct mm_struct *mm,
				unsigned int flags);
void unlock_page(struct page *page);
void folio_unlock(struct folio *folio);

static inline bool folio_trylock(struct folio *folio)
{
	return likely(!test_and_set_bit_lock(PG_locked, folio_flags(folio, 0)));
}

static inline void folio_lock(struct folio *folio)
{
	might_sleep();
	if (!folio_trylock(folio))
		__folio_lock(folio);
}

static inline void lock_page(struct page *page)
{
	struct folio *folio;
	might_sleep();

	folio = page_folio(page);
	if (!folio_trylock(folio))
		__folio_lock(folio);
}

void folio_wait_bit(struct folio *folio, int bit_nr);
int folio_wait_bit_killable(struct folio *folio, int bit_nr);

static inline void folio_wait_locked(struct folio *folio)
{
	if (folio_test_locked(folio))
		folio_wait_bit(folio, PG_locked);
}

static inline int folio_wait_locked_killable(struct folio *folio)
{
	if (!folio_test_locked(folio))
		return 0;
	return folio_wait_bit_killable(folio, PG_locked);
}

/* folio_put_wait_locked removed - always returned 0 */
/* wait_on_page_writeback inlined into single caller in filemap.c */
/* folio_wait_writeback, __folio_cancel_dirty, folio_cancel_dirty, folio_wait_stable,
   folio_account_cleaned, page_cache_sync_readahead removed - empty stubs, calls removed */
void folio_end_writeback(struct folio *folio);
void folio_invalidate(struct folio *folio, size_t offset, size_t length);
/* __set_page_dirty_nobuffers, noop_dirty_folio removed - unused */
size_t fault_in_readable(const char __user *uaddr, size_t size);

int filemap_add_folio(struct address_space *mapping, struct folio *folio,
		pgoff_t index, gfp_t gfp);
void filemap_remove_folio(struct folio *folio);
void __filemap_remove_folio(struct folio *folio, void *shadow);
void delete_from_page_cache_batch(struct address_space *mapping,
				  struct folio_batch *fbatch);
bool filemap_release_folio(struct folio *folio, gfp_t gfp);

int __filemap_add_folio(struct address_space *mapping, struct folio *folio,
		pgoff_t index, gfp_t gfp, void **shadowp);

#endif
