#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <linux/gfp.h>
#include <linux/hardirq.h>
enum mapping_flags {
	AS_ENOSPC	= 1,
	AS_UNEVICTABLE	= 3,
	AS_EXITING	= 4,
};

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

void release_pages(struct page **pages, int nr);

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

static inline struct page *folio_file_page(struct folio *folio, pgoff_t index)
{
	return folio_page(folio, index & (folio_nr_pages(folio) - 1));
}

static inline loff_t page_offset(struct page *page)
{
	return ((loff_t)page->index) << PAGE_SHIFT;
}

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

void unlock_page(struct page *page);
void folio_unlock(struct folio *folio);

static inline bool folio_trylock(struct folio *folio)
{
	return likely(!test_and_set_bit_lock(PG_locked, folio_flags(folio, 0)));
}

static inline void folio_lock(struct folio *folio)
{
	while (!folio_trylock(folio))
		cpu_relax();
}

static inline void lock_page(struct page *page)
{
	folio_lock(page_folio(page));
}

static inline int folio_wait_locked_killable(struct folio *folio)
{
	if (!folio_test_locked(folio))
		return 0;
	folio_lock(folio);
	folio_unlock(folio);
	return 0;
}

int filemap_add_folio(struct address_space *mapping, struct folio *folio,
		pgoff_t index, gfp_t gfp);

int __filemap_add_folio(struct address_space *mapping, struct folio *folio,
		pgoff_t index, gfp_t gfp, void **shadowp);

#endif
