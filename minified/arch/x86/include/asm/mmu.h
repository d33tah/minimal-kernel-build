 
#ifndef _ASM_X86_MMU_H
#define _ASM_X86_MMU_H

#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/bits.h>

/* MM_CONTEXT_UPROBE_IA32, MM_CONTEXT_HAS_VSYSCALL removed - never used */

 
typedef struct {
	 
	u64 ctx_id;

	 
	atomic64_t tlb_gen;



	struct mutex lock;
	void __user *vdso;			 
	const struct vdso_image *vdso_image;	 

	/* perf_rdpmc_allowed removed - never written, rdpmc_never_available_key is TRUE */
} mm_context_t;

#define INIT_MM_CONTEXT(mm)						\
	.context = {							\
		.ctx_id = 1,						\
		.lock = __MUTEX_INITIALIZER(mm.context.lock),		\
	}

/* leave_mm removed - never called */

#endif  
