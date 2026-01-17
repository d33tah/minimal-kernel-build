 
 

#ifndef _ASM_X86_THREAD_INFO_H
#define _ASM_X86_THREAD_INFO_H

#include <linux/compiler.h>
#include <asm/page.h>
#include <asm/percpu.h>
#include <asm/types.h>

 
#  define TOP_OF_KERNEL_STACK_PADDING 8

 
#ifndef __ASSEMBLY__
struct task_struct;
#include <asm/cpufeature.h>
#include <linux/atomic.h>

struct thread_info {
	unsigned long		flags;
	unsigned long		syscall_work;
	/* status field removed - unused after TS_COMPAT removal */
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags		= 0,			\
}

#else  

#include <asm/asm-offsets.h>

#endif

 
#define TIF_NOTIFY_RESUME	1
#define TIF_SIGPENDING		2
#define TIF_NEED_RESCHED	3
/* TIF_SINGLESTEP removed - never used */
#define TIF_SSBD		5
#define TIF_SPEC_IB		9
#define TIF_SPEC_L1D_FLUSH	10
#define TIF_USER_RETURN_NOTIFY	11
#define TIF_UPROBE		12
#define TIF_PATCH_PENDING	13
#define TIF_NEED_FPU_LOAD	14
#define TIF_NOCPUID		15
#define TIF_NOTSC		16
#define TIF_NOTIFY_SIGNAL	17
/* TIF_MEMDIE removed - unused (OOM killer disabled) */
#define TIF_POLLING_NRFLAG	21
/* TIF_IO_BITMAP removed - unused (no I/O bitmap support) */
#define TIF_SPEC_FORCE_UPDATE	23
/* TIF_FORCED_TF removed - unused (no ptrace) */
#define TIF_BLOCKSTEP		25
/* TIF_LAZY_MMU_UPDATES removed - unused (paravirt disabled) */
/* TIF_ADDR32 removed - unused (IA32 emulation disabled) */

#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
/* _TIF_SINGLESTEP removed - never used */
#define _TIF_SSBD		(1 << TIF_SSBD)
/* _TIF_SPEC_IB, _TIF_SPEC_L1D_FLUSH removed - never used */
#define _TIF_USER_RETURN_NOTIFY	(1 << TIF_USER_RETURN_NOTIFY)
#define _TIF_UPROBE		(1 << TIF_UPROBE)
#define _TIF_PATCH_PENDING	(1 << TIF_PATCH_PENDING)
#define _TIF_NEED_FPU_LOAD	(1 << TIF_NEED_FPU_LOAD)
#define _TIF_NOCPUID		(1 << TIF_NOCPUID)
#define _TIF_NOTSC		(1 << TIF_NOTSC)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)
/* _TIF_POLLING_NRFLAG removed - never used */
#define _TIF_SPEC_FORCE_UPDATE	(1 << TIF_SPEC_FORCE_UPDATE)
#define _TIF_BLOCKSTEP		(1 << TIF_BLOCKSTEP)

 
#define _TIF_WORK_CTXSW_BASE					\
	(_TIF_NOCPUID | _TIF_NOTSC | _TIF_BLOCKSTEP |		\
	 _TIF_SSBD | _TIF_SPEC_FORCE_UPDATE)

 
# define _TIF_WORK_CTXSW	(_TIF_WORK_CTXSW_BASE)

# define _TIF_WORK_CTXSW_PREV	(_TIF_WORK_CTXSW| _TIF_USER_RETURN_NOTIFY)

#define _TIF_WORK_CTXSW_NEXT	(_TIF_WORK_CTXSW)

/* STACK_WARN removed - never used */

#ifndef __ASSEMBLY__

/* arch_within_stack_frames removed - unused */

#endif   

/* TS_COMPAT removed - never used (IA32_EMULATION disabled) */

#ifndef __ASSEMBLY__

#define in_ia32_syscall() true

/* arch_task_cache_init, arch_release_task_struct removed - declared but never defined/used */
extern int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src);
extern void arch_setup_new_exec(void);
#define arch_setup_new_exec arch_setup_new_exec
#endif	 

#endif  
