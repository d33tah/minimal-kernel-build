/* Minimal mempolicy.h - all stubs */
#ifndef _LINUX_MEMPOLICY_H
#define _LINUX_MEMPOLICY_H 1

#include <linux/nodemask.h>

struct mm_struct;
struct task_struct;
struct vm_area_struct;
struct page;


struct mempolicy {};

static inline bool mpol_equal(struct mempolicy *a, struct mempolicy *b)
{
	return true;
}

static inline void mpol_put(struct mempolicy *p)
{
}

/* mpol_cond_put, mpol_get, mpol_shared_policy_init, mpol_free_shared_policy removed - unused */

struct shared_policy {};

static inline struct mempolicy *
mpol_shared_policy_lookup(struct shared_policy *sp, unsigned long idx)
{
	return NULL;
}

#define vma_policy(vma) NULL

static inline int
vma_dup_policy(struct vm_area_struct *src, struct vm_area_struct *dst)
{
	return 0;
}

static inline void numa_policy_init(void)
{
}

static inline void numa_default_policy(void)
{
}

/* mpol_rebind_task, mpol_rebind_mm, huge_node, init_nodemask_of_mempolicy, do_migrate_pages removed - unused */

static inline void check_highest_zone(int k)
{
}


/* mpol_misplaced, policy_nodemask_current, mpol_is_preferred_many removed - unused */

static inline void mpol_put_task_policy(struct task_struct *task)
{
}

#endif
