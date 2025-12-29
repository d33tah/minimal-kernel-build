/* Minimal mempolicy.h - all stubs */
#ifndef _LINUX_MEMPOLICY_H
#define _LINUX_MEMPOLICY_H 1
#include <linux/nodemask.h>
struct mm_struct;
struct task_struct;
struct vm_area_struct;
struct page;
struct mempolicy {};
static inline bool mpol_equal(struct mempolicy *a, struct mempolicy *b) { return true; }
static inline void mpol_put(struct mempolicy *p) {}
#define vma_policy(vma) NULL
#endif
