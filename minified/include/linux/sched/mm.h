#ifndef _LINUX_SCHED_MM_H
#define _LINUX_SCHED_MM_H

#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/gfp.h>
#include <asm/sync_core.h>


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

static inline void mmget(struct mm_struct *mm)
{
	atomic_inc(&mm->mm_users);
}

/* mmget_not_zero removed - unused */

extern void mmput(struct mm_struct *);

extern struct mm_struct *get_task_mm(struct task_struct *task);

extern void exit_mm_release(struct task_struct *, struct mm_struct *);
extern void exec_mm_release(struct task_struct *, struct mm_struct *);

static inline void mm_update_next_owner(struct mm_struct *mm)
{
}

#ifndef arch_get_mmap_end
#define arch_get_mmap_end(addr, len, flags)	(TASK_SIZE)
#endif

#ifndef arch_get_mmap_base
#define arch_get_mmap_base(addr, base) (base)
#endif

extern void arch_pick_mmap_layout(struct mm_struct *mm,
				  struct rlimit *rlim_stack);
extern unsigned long
arch_get_unmapped_area(struct file *, unsigned long, unsigned long,
		       unsigned long, unsigned long);
extern unsigned long
arch_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
			  unsigned long len, unsigned long pgoff,
			  unsigned long flags);

unsigned long
generic_get_unmapped_area(struct file *filp, unsigned long addr,
			  unsigned long len, unsigned long pgoff,
			  unsigned long flags);
unsigned long
generic_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
				  unsigned long len, unsigned long pgoff,
				  unsigned long flags);

/* in_vfork removed - unused */

static inline gfp_t current_gfp_context(gfp_t flags)
{
	unsigned int pflags = READ_ONCE(current->flags);

	if (unlikely(pflags & (PF_MEMALLOC_NOIO | PF_MEMALLOC_NOFS | PF_MEMALLOC_PIN))) {
		 
		if (pflags & PF_MEMALLOC_NOIO)
			flags &= ~(__GFP_IO | __GFP_FS);
		else if (pflags & PF_MEMALLOC_NOFS)
			flags &= ~__GFP_FS;

		if (pflags & PF_MEMALLOC_PIN)
			flags &= ~__GFP_MOVABLE;
	}
	return flags;
}

/* __fs_reclaim_acquire, __fs_reclaim_release removed - unused */
static inline void fs_reclaim_acquire(gfp_t gfp_mask) { }
static inline void fs_reclaim_release(gfp_t gfp_mask) { }

/* memalloc_retry_wait removed - unused */

static inline void might_alloc(gfp_t gfp_mask)
{
	fs_reclaim_acquire(gfp_mask);
	fs_reclaim_release(gfp_mask);

	might_sleep_if(gfpflags_allow_blocking(gfp_mask));
}

static inline unsigned int memalloc_noio_save(void)
{
	unsigned int flags = current->flags & PF_MEMALLOC_NOIO;
	current->flags |= PF_MEMALLOC_NOIO;
	return flags;
}

static inline void memalloc_noio_restore(unsigned int flags)
{
	current->flags = (current->flags & ~PF_MEMALLOC_NOIO) | flags;
}

static inline unsigned int memalloc_nofs_save(void)
{
	unsigned int flags = current->flags & PF_MEMALLOC_NOFS;
	current->flags |= PF_MEMALLOC_NOFS;
	return flags;
}

static inline void memalloc_nofs_restore(unsigned int flags)
{
	current->flags = (current->flags & ~PF_MEMALLOC_NOFS) | flags;
}

/* memalloc_noreclaim_save, memalloc_noreclaim_restore removed - unused */

static inline unsigned int memalloc_pin_save(void)
{
	unsigned int flags = current->flags & PF_MEMALLOC_PIN;

	current->flags |= PF_MEMALLOC_PIN;
	return flags;
}

static inline void memalloc_pin_restore(unsigned int flags)
{
	current->flags = (current->flags & ~PF_MEMALLOC_PIN) | flags;
}

/* set_active_memcg removed - unused */

static inline void membarrier_exec_mmap(struct mm_struct *mm)
{
}
static inline void membarrier_mm_sync_core_before_usermode(struct mm_struct *mm)
{
}
static inline void membarrier_update_current_mm(struct mm_struct *next_mm)
{
}

static inline void mm_pasid_init(struct mm_struct *mm) {}
/* mm_pasid_set removed - unused */
static inline void mm_pasid_drop(struct mm_struct *mm) {}

#endif  
