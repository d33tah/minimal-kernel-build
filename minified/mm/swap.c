#include <linux/mm.h>

void __put_page(struct page *page)
{
	/* runtime-dead: nothing is ever freed on this single-shot boot */
}
