#ifndef _LINUX_MMU_NOTIFIER_H
#define _LINUX_MMU_NOTIFIER_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mm_types.h>
#include <linux/mmap_lock.h>
#include <linux/srcu.h>
#include <linux/rbtree.h>

/* Most forward declarations removed - unused in minimal kernel */

struct mmu_notifier_range {
	unsigned long start;
	unsigned long end;
};

static inline void _mmu_notifier_range_init(struct mmu_notifier_range *range,
					    unsigned long start,
					    unsigned long end)
{
	range->start = start;
	range->end = end;
}

#define mmu_notifier_range_init(range,event,flags,vma,mm,start,end)  \
	_mmu_notifier_range_init(range, start, end)
/* mmu_notifier_range_init_owner removed - never used */


static inline void mmu_notifier_release(struct mm_struct *mm)
{
}

static inline void
mmu_notifier_invalidate_range_start(struct mmu_notifier_range *range)
{
}


static inline
void mmu_notifier_invalidate_range_end(struct mmu_notifier_range *range)
{
}

/* mmu_notifier_invalidate_range removed - empty stub, no callers */

static inline void mmu_notifier_subscriptions_init(struct mm_struct *mm)
{
}

static inline void mmu_notifier_subscriptions_destroy(struct mm_struct *mm)
{
}

#endif  
