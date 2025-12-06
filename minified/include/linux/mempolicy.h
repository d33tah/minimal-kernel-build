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

static inline void mpol_cond_put(struct mempolicy *pol)
{
}

static inline void mpol_get(struct mempolicy *pol)
{
}

struct shared_policy {};

static inline void mpol_shared_policy_init(struct shared_policy *sp,
						struct mempolicy *mpol)
{
}

static inline void mpol_free_shared_policy(struct shared_policy *p)
{
}

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

static inline void mpol_rebind_task(struct task_struct *tsk,
				const nodemask_t *new)
{
}

static inline void mpol_rebind_mm(struct mm_struct *mm, nodemask_t *new)
{
}

static inline int huge_node(struct vm_area_struct *vma,
				unsigned long addr, gfp_t gfp_flags,
				struct mempolicy **mpol, nodemask_t **nodemask)
{
	*mpol = NULL;
	*nodemask = NULL;
	return 0;
}

static inline bool init_nodemask_of_mempolicy(nodemask_t *m)
{
	return false;
}

static inline int do_migrate_pages(struct mm_struct *mm, const nodemask_t *from,
				   const nodemask_t *to, int flags)
{
	return 0;
}

static inline void check_highest_zone(int k)
{
}


static inline int mpol_misplaced(struct page *page, struct vm_area_struct *vma,
				 unsigned long address)
{
	return -1;  
}

static inline void mpol_put_task_policy(struct task_struct *task)
{
}

static inline nodemask_t *policy_nodemask_current(gfp_t gfp)
{
	return NULL;
}

static inline bool mpol_is_preferred_many(struct mempolicy *pol)
{
	return  false;
}

#endif
