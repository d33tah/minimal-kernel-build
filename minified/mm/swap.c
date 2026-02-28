
#include <linux/mm.h>

void __put_page(struct page *page)
{
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
	for (i = 0; i < nr; i++)
		put_page_testzero(pages[i]);
}
