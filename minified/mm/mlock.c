/* Minimal includes for mlock stubs */
#include <linux/mm.h>

bool can_do_mlock(void)
{
	return false;
}

void mlock_page_drain_local(void)
{
	 
}

void mlock_page_drain_remote(int cpu)
{
	 
}

void mlock_folio(struct folio *folio)
{
	 
}

void mlock_new_page(struct page *page)
{
	 
}
