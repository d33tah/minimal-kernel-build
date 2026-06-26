 
#ifndef _ASM_X86_UNWIND_H
#define _ASM_X86_UNWIND_H

#include <linux/sched.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>

#define IRET_FRAME_OFFSET (offsetof(struct pt_regs, ip))

/* struct unwind_state + unwind_recover_rethook/ret_addr inlines removed - 0-ref */


#define READ_ONCE_TASK_STACK(task, x)			\
({							\
	unsigned long val;				\
	if (task == current)				\
		val = READ_ONCE(x);			\
	else						\
		val = READ_ONCE_NOCHECK(x);		\
	val;						\
})

#endif
