/* linux/migrate.h removed - empty header */
#include <linux/pagemap.h>
#include <linux/swap.h>
#include "internal.h"

/* page_mapping removed - never called */

void unlock_page(struct page *page)
{
	return folio_unlock(page_folio(page));
}

/* wait_on_page_writeback inlined into single caller (~4 LOC) */

/* page_mapped removed - never called */
/* mark_page_accessed inlined into single caller (~4 LOC) */

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
/* grab_cache_page_write_begin removed - inlined into single caller */
