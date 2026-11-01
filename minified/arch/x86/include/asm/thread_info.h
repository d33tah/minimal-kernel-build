#ifndef _ASM_X86_THREAD_INFO_H
#define _ASM_X86_THREAD_INFO_H

#include <asm/page.h>

#define TOP_OF_KERNEL_STACK_PADDING 8

#ifndef __ASSEMBLY__
struct task_struct;

struct thread_info {
	unsigned long flags;
};

#define INIT_THREAD_INFO(tsk) \
	{                     \
		.flags = 0,   \
	}

#else

#include <asm/asm-offsets.h>

#endif

#define TIF_NEED_RESCHED 3

#define _TIF_NEED_RESCHED (1 << TIF_NEED_RESCHED)

#ifndef __ASSEMBLY__

extern int arch_dup_task_struct(struct task_struct *dst,
				struct task_struct *src);
#endif

#endif
