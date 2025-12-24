#ifndef _LINUX_MIGRATE_H
#define _LINUX_MIGRATE_H
#include <linux/mm.h>
#include <linux/mempolicy.h>
#include <linux/hugetlb.h>
typedef struct page *new_page_t(struct page *page, unsigned long private);
typedef void free_page_t(struct page *page, unsigned long private);
struct migration_target_control;
static inline int migrate_pages(struct list_head *l, new_page_t new, free_page_t free, unsigned long private, enum migrate_mode mode, int reason, unsigned int *ret_succeeded) { return -ENOSYS; }
static inline void migrate_on_reclaim_init(void) {}
#endif  
