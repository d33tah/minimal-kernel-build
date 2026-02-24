
#include <linux/mm.h>

/* Simplified for hello-world kernel: no LRU tracking, no page reclaim */

void __put_page(struct page *page)
{
	if (unlikely(PageCompound(page)))
		destroy_compound_page(page);
}

void folio_add_lru(struct folio *folio)
{
}

void lru_cache_add_inactive_or_unevictable(struct page *page,
					   struct vm_area_struct *vma)
{
}

void lru_add_drain(void)
{
}

void release_pages(struct page **pages, int nr)
{
	int i;

	for (i = 0; i < nr; i++) {
		struct page *page = pages[i];

		if (!put_page_testzero(page))
			continue;

		if (PageCompound(page))
			destroy_compound_page(page);
	}
}
