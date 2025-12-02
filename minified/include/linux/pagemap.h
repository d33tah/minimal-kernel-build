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
#include <linux/hugetlb_inline.h>

struct folio_batch;

unsigned long invalidate_mapping_pages(struct address_space *mapping,
					pgoff_t start, pgoff_t end);

int invalidate_inode_pages2(struct address_space *mapping);
int invalidate_inode_pages2_range(struct address_space *mapping,
		pgoff_t start, pgoff_t end);
int write_inode_now(struct inode *, int sync);
int filemap_fdatawrite(struct address_space *);
int filemap_flush(struct address_space *);
int filemap_fdatawait_keep_errors(struct address_space *mapping);
int filemap_fdatawait_range(struct address_space *, loff_t lstart, loff_t lend);
int filemap_fdatawait_range_keep_errors(struct address_space *mapping,
		loff_t start_byte, loff_t end_byte);

bool filemap_range_has_page(struct address_space *, loff_t lstart, loff_t lend);
int filemap_write_and_wait_range(struct address_space *mapping,
		loff_t lstart, loff_t lend);
int __filemap_fdatawrite_range(struct address_space *mapping,
		loff_t start, loff_t end, int sync_mode);
int filemap_fdatawrite_range(struct address_space *mapping,
		loff_t start, loff_t end);
int filemap_check_errors(struct address_space *mapping);
void __filemap_set_wb_err(struct address_space *mapping, int err);
int filemap_fdatawrite_wbc(struct address_space *mapping,
			   struct writeback_control *wbc);

static inline errseq_t filemap_sample_wb_err(struct address_space *mapping)
{
	return errseq_sample(&mapping->wb_err);
}

static inline errseq_t file_sample_sb_err(struct file *file)
{
	return errseq_sample(&file->f_path.dentry->d_sb->s_wb_err);
}

static inline bool mapping_empty(struct address_space *mapping)
{
	return xa_empty(&mapping->i_pages);
}

static inline bool mapping_shrinkable(struct address_space *mapping)
{
	void *head;

	 
	if (IS_ENABLED(CONFIG_HIGHMEM))
		return true;

	 
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
	AS_MM_ALL_LOCKS	= 2,	 
	AS_UNEVICTABLE	= 3,	 
	AS_EXITING	= 4, 	 
	 
	AS_NO_WRITEBACK_TAGS = 5,
	AS_LARGE_FOLIO_SUPPORT = 6,
};

static inline void mapping_set_unevictable(struct address_space *mapping)
{
	set_bit(AS_UNEVICTABLE, &mapping->flags);
}

static inline bool mapping_unevictable(struct address_space *mapping)
{
	return mapping && test_bit(AS_UNEVICTABLE, &mapping->flags);
}

static inline void mapping_set_exiting(struct address_space *mapping)
{
	set_bit(AS_EXITING, &mapping->flags);
}

static inline int mapping_exiting(struct address_space *mapping)
{
	return test_bit(AS_EXITING, &mapping->flags);
}

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


static inline bool mapping_large_folio_support(struct address_space *mapping)
{
	return IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
		test_bit(AS_LARGE_FOLIO_SUPPORT, &mapping->flags);
}

static inline int filemap_nr_thps(struct address_space *mapping)
{
	return 0;
}

static inline void filemap_nr_thps_dec(struct address_space *mapping)
{
	WARN_ON_ONCE(mapping_large_folio_support(mapping) == 0);
}

void release_pages(struct page **pages, int nr);

struct address_space *page_mapping(struct page *);
struct address_space *folio_mapping(struct folio *);
struct address_space *swapcache_mapping(struct folio *);

static inline struct address_space *folio_file_mapping(struct folio *folio)
{
	if (unlikely(folio_test_swapcache(folio)))
		return swapcache_mapping(folio);

	return folio->mapping;
}

static inline struct address_space *page_file_mapping(struct page *page)
{
	return folio_file_mapping(page_folio(page));
}

static inline struct folio *filemap_alloc_folio(gfp_t gfp, unsigned int order)
{
	return folio_alloc(gfp, order);
}

typedef int filler_t(struct file *, struct folio *);

pgoff_t page_cache_next_miss(struct address_space *mapping,
			     pgoff_t index, unsigned long max_scan);
pgoff_t page_cache_prev_miss(struct address_space *mapping,
			     pgoff_t index, unsigned long max_scan);

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

static inline struct folio *filemap_get_folio(struct address_space *mapping,
					pgoff_t index)
{
	return __filemap_get_folio(mapping, index, 0, 0);
}

static inline struct folio *filemap_lock_folio(struct address_space *mapping,
					pgoff_t index)
{
	return __filemap_get_folio(mapping, index, FGP_LOCK, 0);
}

static inline struct page *find_get_page(struct address_space *mapping,
					pgoff_t offset)
{
	return pagecache_get_page(mapping, offset, 0, 0);
}

static inline struct page *find_lock_page(struct address_space *mapping,
					pgoff_t index)
{
	return pagecache_get_page(mapping, index, FGP_LOCK, 0);
}

#define swapcache_index(folio)	__page_file_index(&(folio)->page)

static inline pgoff_t folio_index(struct folio *folio)
{
        if (unlikely(folio_test_swapcache(folio)))
                return swapcache_index(folio);
        return folio->index;
}


static inline struct page *folio_file_page(struct folio *folio, pgoff_t index)
{
	 
	if (folio_test_hugetlb(folio))
		return &folio->page;
	return folio_page(folio, index & (folio_nr_pages(folio) - 1));
}

static inline bool folio_contains(struct folio *folio, pgoff_t index)
{
	 
	if (folio_test_hugetlb(folio))
		return folio->index == index;
	return index - folio_index(folio) < folio_nr_pages(folio);
}

unsigned find_get_pages_range(struct address_space *mapping, pgoff_t *start,
			pgoff_t end, unsigned int nr_pages,
			struct page **pages);
unsigned find_get_pages_contig(struct address_space *mapping, pgoff_t start,
			       unsigned int nr_pages, struct page **pages);
unsigned find_get_pages_range_tag(struct address_space *mapping, pgoff_t *index,
			pgoff_t end, xa_mark_t tag, unsigned int nr_pages,
			struct page **pages);
struct page *grab_cache_page_write_begin(struct address_space *mapping,
			pgoff_t index);

struct folio *read_cache_folio(struct address_space *, pgoff_t index,
		filler_t *filler, struct file *file);
struct page *read_cache_page(struct address_space *, pgoff_t index,
		filler_t *filler, struct file *file);
extern struct page * read_cache_page_gfp(struct address_space *mapping,
				pgoff_t index, gfp_t gfp_mask);

static inline struct page *read_mapping_page(struct address_space *mapping,
				pgoff_t index, struct file *file)
{
	return read_cache_page(mapping, index, NULL, file);
}

static inline pgoff_t page_to_index(struct page *page)
{
	struct page *head;

	if (likely(!PageTransTail(page)))
		return page->index;

	head = compound_head(page);
	return head->index + page - head;
}

extern pgoff_t hugetlb_basepage_index(struct page *page);

static inline pgoff_t page_to_pgoff(struct page *page)
{
	if (unlikely(PageHuge(page)))
		return hugetlb_basepage_index(page);
	return page_to_index(page);
}

static inline loff_t page_offset(struct page *page)
{
	return ((loff_t)page->index) << PAGE_SHIFT;
}

static inline loff_t page_file_offset(struct page *page)
{
	return ((loff_t)page_index(page)) << PAGE_SHIFT;
}

static inline loff_t folio_pos(struct folio *folio)
{
	return page_offset(&folio->page);
}

static inline loff_t folio_file_pos(struct folio *folio)
{
	return page_file_offset(&folio->page);
}

static inline pgoff_t folio_pgoff(struct folio *folio)
{
	if (unlikely(folio_test_hugetlb(folio)))
		return hugetlb_basepage_index(&folio->page);
	return folio->index;
}

extern pgoff_t linear_hugepage_index(struct vm_area_struct *vma,
				     unsigned long address);

static inline pgoff_t linear_page_index(struct vm_area_struct *vma,
					unsigned long address)
{
	pgoff_t pgoff;
	if (unlikely(is_vm_hugetlb_page(vma)))
		return linear_hugepage_index(vma, address);
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

static inline bool wake_page_match(struct wait_page_queue *wait_page,
				  struct wait_page_key *key)
{
	if (wait_page->folio != key->folio)
	       return false;
	key->page_match = 1;

	if (wait_page->bit_nr != key->bit_nr)
		return false;

	return true;
}

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

int folio_put_wait_locked(struct folio *folio, int state);
void wait_on_page_writeback(struct page *page);
void folio_wait_writeback(struct folio *folio);
int folio_wait_writeback_killable(struct folio *folio);
void end_page_writeback(struct page *page);
void folio_end_writeback(struct folio *folio);
void wait_for_stable_page(struct page *page);
void folio_wait_stable(struct folio *folio);
void __folio_mark_dirty(struct folio *folio, struct address_space *, int warn);
void folio_account_cleaned(struct folio *folio, struct bdi_writeback *wb);
void __folio_cancel_dirty(struct folio *folio);
static inline void folio_cancel_dirty(struct folio *folio)
{
	 
	if (folio_test_dirty(folio))
		__folio_cancel_dirty(folio);
}
bool folio_clear_dirty_for_io(struct folio *folio);
bool clear_page_dirty_for_io(struct page *page);
void folio_invalidate(struct folio *folio, size_t offset, size_t length);
int __must_check folio_write_one(struct folio *folio);
int __set_page_dirty_nobuffers(struct page *page);
bool noop_dirty_folio(struct address_space *mapping, struct folio *folio);

void page_endio(struct page *page, bool is_write, int err);

void folio_end_private_2(struct folio *folio);
void folio_wait_private_2(struct folio *folio);
int folio_wait_private_2_killable(struct folio *folio);

void folio_add_wait_queue(struct folio *folio, wait_queue_entry_t *waiter);

size_t fault_in_writeable(char __user *uaddr, size_t size);
size_t fault_in_subpage_writeable(char __user *uaddr, size_t size);
size_t fault_in_safe_writeable(const char __user *uaddr, size_t size);
size_t fault_in_readable(const char __user *uaddr, size_t size);

int add_to_page_cache_locked(struct page *page, struct address_space *mapping,
		pgoff_t index, gfp_t gfp);
int add_to_page_cache_lru(struct page *page, struct address_space *mapping,
		pgoff_t index, gfp_t gfp);
int filemap_add_folio(struct address_space *mapping, struct folio *folio,
		pgoff_t index, gfp_t gfp);
void filemap_remove_folio(struct folio *folio);
void delete_from_page_cache(struct page *page);
void __filemap_remove_folio(struct folio *folio, void *shadow);
static inline void __delete_from_page_cache(struct page *page, void *shadow)
{
	__filemap_remove_folio(page_folio(page), shadow);
}
void replace_page_cache_page(struct page *old, struct page *new);
void delete_from_page_cache_batch(struct address_space *mapping,
				  struct folio_batch *fbatch);
int try_to_release_page(struct page *page, gfp_t gfp);
bool filemap_release_folio(struct folio *folio, gfp_t gfp);
loff_t mapping_seek_hole_data(struct address_space *, loff_t start, loff_t end,
		int whence);

int __filemap_add_folio(struct address_space *mapping, struct folio *folio,
		pgoff_t index, gfp_t gfp, void **shadowp);

bool filemap_range_has_writeback(struct address_space *mapping,
				 loff_t start_byte, loff_t end_byte);

static inline bool filemap_range_needs_writeback(struct address_space *mapping,
						 loff_t start_byte,
						 loff_t end_byte)
{
	if (!mapping->nrpages)
		return false;
	if (!mapping_tagged(mapping, PAGECACHE_TAG_DIRTY) &&
	    !mapping_tagged(mapping, PAGECACHE_TAG_WRITEBACK))
		return false;
	return filemap_range_has_writeback(mapping, start_byte, end_byte);
}

struct readahead_control {
	struct file *file;
	struct address_space *mapping;
	struct file_ra_state *ra;
	pgoff_t _index;
	unsigned int _nr_pages;
	unsigned int _batch_count;
};

#define DEFINE_READAHEAD(ractl, f, r, m, i)				\
	struct readahead_control ractl = {				\
		.file = f,						\
		.mapping = m,						\
		.ra = r,						\
		._index = i,						\
	}

#define VM_READAHEAD_PAGES	(SZ_128K / PAGE_SIZE)

void page_cache_ra_unbounded(struct readahead_control *,
		unsigned long nr_to_read, unsigned long lookahead_count);
void page_cache_sync_ra(struct readahead_control *, unsigned long req_count);
void page_cache_async_ra(struct readahead_control *, struct folio *,
		unsigned long req_count);
void readahead_expand(struct readahead_control *ractl,
		      loff_t new_start, size_t new_len);

static inline
void page_cache_sync_readahead(struct address_space *mapping,
		struct file_ra_state *ra, struct file *file, pgoff_t index,
		unsigned long req_count)
{
	DEFINE_READAHEAD(ractl, file, ra, mapping, index);
	page_cache_sync_ra(&ractl, req_count);
}

static inline
void page_cache_async_readahead(struct address_space *mapping,
		struct file_ra_state *ra, struct file *file,
		struct folio *folio, pgoff_t index, unsigned long req_count)
{
	DEFINE_READAHEAD(ractl, file, ra, mapping, index);
	page_cache_async_ra(&ractl, folio, req_count);
}

#endif
