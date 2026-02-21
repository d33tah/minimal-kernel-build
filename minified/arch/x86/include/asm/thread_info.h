 
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
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags		= 0,			\
}

#else  

#include <generated/asm-offsets.h>

#endif

#define TIF_NOTIFY_RESUME	1
#define TIF_SIGPENDING		2
#define TIF_NEED_RESCHED	3
#define TIF_SPEC_IB		9
#define TIF_SPEC_L1D_FLUSH	10
#define TIF_UPROBE		12
#define TIF_PATCH_PENDING	13
#define TIF_NEED_FPU_LOAD	14
#define TIF_NOTIFY_SIGNAL	17
#define TIF_POLLING_NRFLAG	21

#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_UPROBE		(1 << TIF_UPROBE)
#define _TIF_PATCH_PENDING	(1 << TIF_PATCH_PENDING)
#define _TIF_NEED_FPU_LOAD	(1 << TIF_NEED_FPU_LOAD)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)

#define _TIF_WORK_CTXSW	(0)

#define _TIF_WORK_CTXSW_PREV	(_TIF_WORK_CTXSW)

#define _TIF_WORK_CTXSW_NEXT	(_TIF_WORK_CTXSW)

#ifndef __ASSEMBLY__

#endif   

#ifndef __ASSEMBLY__

extern int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src);
extern void arch_setup_new_exec(void);
#define arch_setup_new_exec arch_setup_new_exec
#endif	 

#endif  
