 
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


static inline void putback_movable_pages(struct list_head *l) {}
static inline int migrate_pages(struct list_head *l, new_page_t new,
		free_page_t free, unsigned long private, enum migrate_mode mode,
		int reason, unsigned int *ret_succeeded)
	{ return -ENOSYS; }
static inline struct page *alloc_migration_target(struct page *page,
		unsigned long private)
	{ return NULL; }
static inline int isolate_movable_page(struct page *page, isolate_mode_t mode)
	{ return -EBUSY; }

static inline void migrate_page_states(struct page *newpage, struct page *page)
{
}

static inline void migrate_page_copy(struct page *newpage,
				     struct page *page) {}

static inline int migrate_huge_page_move_mapping(struct address_space *mapping,
				  struct page *newpage, struct page *page)
{
	return -ENOSYS;
}


static inline void set_migration_target_nodes(void) {}
static inline void migrate_on_reclaim_init(void) {}
static inline int next_demotion_node(int node)
{
        return NUMA_NO_NODE;
}
#define numa_demotion_enabled  false

static inline int PageMovable(struct page *page) { return 0; }
static inline void __SetPageMovable(struct page *page,
				struct address_space *mapping)
{
}
static inline void __ClearPageMovable(struct page *page)
{
}

static inline bool folio_test_movable(struct folio *folio)
{
	return PageMovable(&folio->page);
}

static inline int migrate_misplaced_page(struct page *page,
					 struct vm_area_struct *vma, int node)
{
	return -EAGAIN;  
}


#endif  
