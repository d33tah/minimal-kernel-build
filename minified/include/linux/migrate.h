 
#ifndef _LINUX_MIGRATE_H
#define _LINUX_MIGRATE_H

#include <linux/mm.h>
#include <linux/mempolicy.h>
#include <linux/migrate_mode.h>
#include <linux/hugetlb.h>

typedef struct page *new_page_t(struct page *page, unsigned long private);
typedef void free_page_t(struct page *page, unsigned long private);

struct migration_target_control;

 
#define MIGRATEPAGE_SUCCESS		0

 
extern const char *migrate_reason_names[MR_TYPES];


/* putback_movable_pages removed - unused */
static inline int migrate_pages(struct list_head *l, new_page_t new,
		free_page_t free, unsigned long private, enum migrate_mode mode,
		int reason, unsigned int *ret_succeeded)
	{ return -ENOSYS; }
/* alloc_migration_target, isolate_movable_page removed - unused */
/* migrate_page_states, migrate_page_copy, migrate_huge_page_move_mapping removed - unused */
/* set_migration_target_nodes removed - unused */
static inline void migrate_on_reclaim_init(void) {}
/* next_demotion_node, numa_demotion_enabled removed - unused */
/* PageMovable, __SetPageMovable, __ClearPageMovable removed - unused */
/* folio_test_movable, migrate_misplaced_page removed - unused */


#endif  
