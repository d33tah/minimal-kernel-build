 
#ifndef _ASM_X86_MMU_H
#define _ASM_X86_MMU_H

#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/bits.h>

typedef struct {
	 
	u64 ctx_id;

	atomic64_t tlb_gen;

	struct mutex lock;
	void __user *vdso;			 
	const struct vdso_image *vdso_image;	 

} mm_context_t;

#define INIT_MM_CONTEXT(mm)						\
	.context = {							\
		.ctx_id = 1,						\
		.lock = __MUTEX_INITIALIZER(mm.context.lock),		\
	}

#endif  
