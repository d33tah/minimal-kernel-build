#ifndef LINUX_MM_INLINE_H
#define LINUX_MM_INLINE_H

#include <linux/swap.h>

static inline void init_tlb_flush_pending(struct mm_struct *mm)
{
	atomic_set(&mm->tlb_flush_pending, 0);
}

#endif
