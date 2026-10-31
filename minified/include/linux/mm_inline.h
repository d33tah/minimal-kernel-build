#ifndef LINUX_MM_INLINE_H
#define LINUX_MM_INLINE_H

#include <linux/swap.h>

static inline void dup_anon_vma_name(struct vm_area_struct *orig_vma,
				     struct vm_area_struct *new_vma)
{
}

static inline void init_tlb_flush_pending(struct mm_struct *mm)
{
	atomic_set(&mm->tlb_flush_pending, 0);
}

static inline void inc_tlb_flush_pending(struct mm_struct *mm)
{
	atomic_inc(&mm->tlb_flush_pending);
}

#endif
