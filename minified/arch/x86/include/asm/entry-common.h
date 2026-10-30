
#ifndef _ASM_X86_ENTRY_COMMON_H
#define _ASM_X86_ENTRY_COMMON_H

#include <linux/sched/task_stack.h>

#include <asm/nospec-branch.h>

static __always_inline void arch_enter_from_user_mode(struct pt_regs *regs)
{
}

static __always_inline void arch_exit_to_user_mode(void)
{
}

#endif
