
#ifndef _ASM_X86_ENTRY_COMMON_H
#define _ASM_X86_ENTRY_COMMON_H

#include <linux/sched/task_stack.h>

#include <asm/nospec-branch.h>
#include <asm/io_bitmap.h>
#include <asm/fpu/api.h>

/* CONFIG_DEBUG_ENTRY not enabled, no debug checks needed */
static __always_inline void arch_enter_from_user_mode(struct pt_regs *regs)
{
}
#define arch_enter_from_user_mode arch_enter_from_user_mode

/* arch_exit_to_user_mode_prepare inlined into kernel/entry/common.c */

static __always_inline void arch_exit_to_user_mode(void)
{
}
#define arch_exit_to_user_mode arch_exit_to_user_mode

#endif
