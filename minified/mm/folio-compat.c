 

#include <linux/migrate.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include "internal.h"

struct address_space *page_mapping(struct page *page)
{
	return folio_mapping(page_folio(page));
}

void unlock_page(struct page *page)
{
	return folio_unlock(page_folio(page));
}

void end_page_writeback(struct page *page)
{
	return folio_end_writeback(page_folio(page));
}

void wait_on_page_writeback(struct page *page)
{
	return folio_wait_writeback(page_folio(page));
}

void wait_for_stable_page(struct page *page)
{
	return folio_wait_stable(page_folio(page));
}

bool page_mapped(struct page *page)
{
	return folio_mapped(page_folio(page));
}

void mark_page_accessed(struct page *page)
{
	folio_mark_accessed(page_folio(page));
}


bool set_page_writeback(struct page *page)
{
	return folio_start_writeback(page_folio(page));
}

bool set_page_dirty(struct page *page)
{
	return folio_mark_dirty(page_folio(page));
}

int __set_page_dirty_nobuffers(struct page *page)
{
	return filemap_dirty_folio(page_mapping(page), page_folio(page));
}

bool clear_page_dirty_for_io(struct page *page)
{
	return folio_clear_dirty_for_io(page_folio(page));
}

bool redirty_page_for_writepage(struct writeback_control *wbc,
		struct page *page)
{
	return folio_redirty_for_writepage(wbc, page_folio(page));
}

void lru_cache_add(struct page *page)
{
	folio_add_lru(page_folio(page));
}

int add_to_page_cache_lru(struct page *page, struct address_space *mapping,
		pgoff_t index, gfp_t gfp)
{
	return filemap_add_folio(mapping, page_folio(page), index, gfp);
}

noinline
struct page *pagecache_get_page(struct address_space *mapping, pgoff_t index,
		int fgp_flags, gfp_t gfp)
{
	struct folio *folio;

	folio = __filemap_get_folio(mapping, index, fgp_flags, gfp);
	if ((fgp_flags & FGP_HEAD) || !folio || xa_is_value(folio))
		return &folio->page;
	return folio_file_page(folio, index);
}

struct page *grab_cache_page_write_begin(struct address_space *mapping,
					pgoff_t index)
{
	unsigned fgp_flags = FGP_LOCK | FGP_WRITE | FGP_CREAT | FGP_STABLE;

	return pagecache_get_page(mapping, index, fgp_flags,
			mapping_gfp_mask(mapping));
}

void delete_from_page_cache(struct page *page)
{
	return filemap_remove_folio(page_folio(page));
}

int try_to_release_page(struct page *page, gfp_t gfp)
{
	return filemap_release_folio(page_folio(page), gfp);
}

int isolate_lru_page(struct page *page)
{
	if (WARN_RATELIMIT(PageTail(page), "trying to isolate tail page"))
		return -EBUSY;
	return folio_isolate_lru((struct folio *)page);
}

void putback_lru_page(struct page *page)
{
	folio_putback_lru(page_folio(page));
}
