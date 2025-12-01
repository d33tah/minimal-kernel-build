#ifndef __LINUX_PAGE_OWNER_H
#define __LINUX_PAGE_OWNER_H

#include <linux/jump_label.h>

static inline void reset_page_owner(struct page *page, unsigned short order)
{
}
static inline void set_page_owner(struct page *page,
			unsigned int order, gfp_t gfp_mask)
{
}
static inline void split_page_owner(struct page *page,
			unsigned short order)
{
}
static inline void folio_copy_owner(struct folio *newfolio, struct folio *folio)
{
}
static inline void set_page_owner_migrate_reason(struct page *page, int reason)
{
}
static inline void dump_page_owner(const struct page *page)
{
}
#endif  
