/* linux/migrate.h removed - empty header */
#include <linux/pagemap.h>
#include <linux/swap.h>
#include "internal.h"

/* page_mapping removed - never called */

void unlock_page(struct page *page)
{
	return folio_unlock(page_folio(page));
}

void wait_on_page_writeback(struct page *page)
{
	return folio_wait_writeback(page_folio(page));
}

bool page_mapped(struct page *page)
{
	return folio_mapped(page_folio(page));
}

void mark_page_accessed(struct page *page)
{
	folio_mark_accessed(page_folio(page));
}

bool set_page_dirty(struct page *page)
{
	return folio_mark_dirty(page_folio(page));
}

/* __set_page_dirty_nobuffers removed - never called */

void lru_cache_add(struct page *page)
{
	folio_add_lru(page_folio(page));
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

struct page *grab_cache_page_write_begin(struct address_space *mapping,
					 pgoff_t index)
{
	unsigned fgp_flags = FGP_LOCK | FGP_WRITE | FGP_CREAT | FGP_STABLE;

	return pagecache_get_page(mapping, index, fgp_flags,
				  mapping_gfp_mask(mapping));
}
