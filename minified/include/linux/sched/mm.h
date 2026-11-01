#ifndef _LINUX_SCHED_MM_H
#define _LINUX_SCHED_MM_H

#include <linux/sched.h>
#include <linux/gfp.h>

extern struct mm_struct *mm_alloc(void);

static inline void mmgrab(struct mm_struct *mm)
{
	atomic_inc(&mm->mm_count);
}

extern void __mmdrop(struct mm_struct *mm);

static inline void mmdrop(struct mm_struct *mm)
{
	if (unlikely(atomic_dec_and_test(&mm->mm_count)))
		__mmdrop(mm);
}

static inline void mmdrop_sched(struct mm_struct *mm)
{
	mmdrop(mm);
}

extern void mmput(struct mm_struct *);

extern void exec_mm_release(struct task_struct *, struct mm_struct *);

static inline gfp_t current_gfp_context(gfp_t flags)
{
	unsigned int pflags = READ_ONCE(current->flags);

	if (unlikely(pflags & (PF_MEMALLOC_NOIO | PF_MEMALLOC_NOFS))) {
		if (pflags & PF_MEMALLOC_NOIO)
			flags &= ~(__GFP_IO | __GFP_FS);
		else if (pflags & PF_MEMALLOC_NOFS)
			flags &= ~__GFP_FS;
	}
	return flags;
}

#endif
