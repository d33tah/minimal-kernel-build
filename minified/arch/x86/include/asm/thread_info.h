 
 

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

#include <asm/asm-offsets.h>

#endif

 
#define TIF_NOTIFY_RESUME	1
#define TIF_SIGPENDING		2
#define TIF_NEED_RESCHED	3
#define TIF_NEED_FPU_LOAD	14
#define TIF_NOTIFY_SIGNAL	17

#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NEED_FPU_LOAD	(1 << TIF_NEED_FPU_LOAD)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)

/*
 * _TIF_WORK_CTXSW_{BASE,PREV,NEXT} + the SSBD/NOTSC/USER_RETURN_NOTIFY/UPROBE/
 * PATCH_PENDING/MEMDIE/SPEC_FORCE_UPDATE bit defs removed - those bits are never
 * set in this build, so the context-switch extra-work path was always a no-op.
 * TIF_NOCPUID/TIF_BLOCKSTEP also removed - never set, their test/clear sites in
 * process.c/traps.c were always-false / no-ops and have been folded away.
 * TIF_POLLING_NRFLAG removed - set/cleared in sched/idle.h but never tested, so
 * the writes were no-ops. TIF_ADDR32 removed - tested in elf.h mmap_is_ia32 but
 * never set, so the COMPAT term was always false.
 */
#define STACK_WARN		(THREAD_SIZE/8)

 
#ifndef __ASSEMBLY__

/* arch_within_stack_frames removed - unused */

#endif   

 
#ifndef __ASSEMBLY__

#define in_ia32_syscall() true

extern void arch_task_cache_init(void);
extern int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src);
extern void arch_setup_new_exec(void);
#define arch_setup_new_exec arch_setup_new_exec
#endif	 

#endif  
