#ifndef PAGEBLOCK_FLAGS_H
#define PAGEBLOCK_FLAGS_H

#include <linux/types.h>

#define PB_migratetype_bits 3
enum pageblock_bits {
	PB_migrate,
	PB_migrate_end = PB_migrate + PB_migratetype_bits - 1,
			 
	PB_migrate_skip, 

	 
	NR_PAGEBLOCK_BITS
};


#define pageblock_order		(MAX_ORDER-1)


#define pageblock_nr_pages	(1UL << pageblock_order)

struct page;

unsigned long get_pfnblock_flags_mask(const struct page *page,
				unsigned long pfn,
				unsigned long mask);

void set_pfnblock_flags_mask(struct page *page,
				unsigned long flags,
				unsigned long pfn,
				unsigned long mask);

static inline bool get_pageblock_skip(struct page *page)
{
	return false;
}
static inline void clear_pageblock_skip(struct page *page)
{
}
static inline void set_pageblock_skip(struct page *page)
{
}

#endif	 
