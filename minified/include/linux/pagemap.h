/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

#include <linux/highmem.h>

static inline gfp_t mapping_gfp_mask(struct address_space *mapping)
{
	return mapping->gfp_mask;
}

/* Restricts the given gfp_mask to what the mapping allows. */
static inline gfp_t mapping_gfp_constraint(struct address_space *mapping,
					   gfp_t gfp_mask)
{
	return mapping_gfp_mask(mapping) & gfp_mask;
}

/*
 * This is non-atomic.  Only to be used before the mapping is activated.
 * Probably needs a barrier...
 */
static inline void mapping_set_gfp_mask(struct address_space *m, gfp_t mask)
{
	m->gfp_mask = mask;
}

static inline struct folio *filemap_alloc_folio(gfp_t gfp, unsigned int order)
{
	return folio_alloc(gfp, order);
}

#define FGP_LOCK 0x00000002
#define FGP_CREAT 0x00000004
#define FGP_WRITE 0x00000008
#define FGP_FOR_MMAP 0x00000040
#define FGP_HEAD 0x00000080
#define FGP_STABLE 0x00000200

struct folio *__filemap_get_folio(struct address_space *mapping, pgoff_t index,
				  int fgp_flags, gfp_t gfp);

/**
 * folio_file_page - The page for a particular index.
 * @folio: The folio which contains this index.
 * @index: The index we want to look up.
 *
 * Sometimes after looking up a folio in the page cache, we need to
 * obtain the specific page for an index (eg a page fault).
 *
 * Return: The page containing the file data for this index.
 */
static inline struct page *folio_file_page(struct folio *folio, pgoff_t index)
{
	return folio_page(folio, index & (folio_nr_pages(folio) - 1));
}

struct page *grab_cache_page_write_begin(struct address_space *mapping,
					 pgoff_t index);

/*
 * Return byte-offset into filesystem object for page.
 */
static inline loff_t page_offset(struct page *page)
{
	return ((loff_t)page->index) << PAGE_SHIFT;
}

/**
 * folio_pos - Returns the byte position of this folio in its file.
 * @folio: The folio.
 */
static inline loff_t folio_pos(struct folio *folio)
{
	return page_offset(&folio->page);
}

static inline pgoff_t linear_page_index(struct vm_area_struct *vma,
					unsigned long address)
{
	pgoff_t pgoff;
	pgoff = (address - vma->vm_start) >> PAGE_SHIFT;
	pgoff += vma->vm_pgoff;
	return pgoff;
}

void __folio_lock(struct folio *folio);
void unlock_page(struct page *page);
void folio_unlock(struct folio *folio);

/**
 * folio_trylock() - Attempt to lock a folio.
 * @folio: The folio to attempt to lock.
 *
 * Sometimes it is undesirable to wait for a folio to be unlocked (eg
 * when the locks are being taken in the wrong order, or if making
 * progress through a batch of folios is more important than processing
 * them in order).  Usually folio_lock() is the correct function to call.
 *
 * Context: Any context.
 * Return: Whether the lock was successfully acquired.
 */
static inline bool folio_trylock(struct folio *folio)
{
	return likely(!test_and_set_bit_lock(PG_locked, folio_flags(folio, 0)));
}

/**
 * folio_lock() - Lock this folio.
 * @folio: The folio to lock.
 *
 * The folio lock protects against many things, probably more than it
 * should.  It is primarily held while a folio is being brought uptodate,
 * either from its backing file or from swap.  It is also held while a
 * folio is being truncated from its address_space, so holding the lock
 * is sufficient to keep folio->mapping stable.
 *
 * The folio lock is also held while write() is modifying the page to
 * provide POSIX atomicity guarantees (as long as the write does not
 * cross a page boundary).  Other modifications to the data in the folio
 * do not hold the folio lock and can race with writes, eg DMA and stores
 * to mapped pages.
 *
 * Context: May sleep.  If you need to acquire the locks of two or
 * more folios, they must be in order of ascending index, if they are
 * in the same address_space.  If they are in different address_spaces,
 * acquire the lock of the folio which belongs to the address_space which
 * has the lowest address in memory first.
 */
static inline void folio_lock(struct folio *folio)
{
	if (!folio_trylock(folio))
		__folio_lock(folio);
}

#endif /* _LINUX_PAGEMAP_H */
