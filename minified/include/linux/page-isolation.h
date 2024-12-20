/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PAGEISOLATION_H
#define __LINUX_PAGEISOLATION_H

static inline bool has_isolate_pageblock(struct zone *zone)
{
	return false;
}
static inline bool is_migrate_isolate_page(struct page *page)
{
	return false;
}
static inline bool is_migrate_isolate(int migratetype)
{
	return false;
}

#define MEMORY_OFFLINE	0x1
#define REPORT_FAILURE	0x2

void set_pageblock_migratetype(struct page *page, int migratetype);
int move_freepages_block(struct zone *zone, struct page *page,
				int migratetype, int *num_movable);

/*
 * Changes migrate type in [start_pfn, end_pfn) to be MIGRATE_ISOLATE.
 */
int
start_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			 int migratetype, int flags, gfp_t gfp_flags);

/*
 * Changes MIGRATE_ISOLATE to MIGRATE_MOVABLE.
 * target range is [start_pfn, end_pfn)
 */
void
undo_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			int migratetype);

/*
 * Test all pages in [start_pfn, end_pfn) are isolated or not.
 */
int test_pages_isolated(unsigned long start_pfn, unsigned long end_pfn,
			int isol_flags);

struct page *alloc_migrate_target(struct page *page, unsigned long private);

#endif
