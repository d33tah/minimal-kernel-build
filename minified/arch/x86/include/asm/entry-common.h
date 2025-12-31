
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

static inline void arch_exit_to_user_mode_prepare(struct pt_regs *regs,
						  unsigned long ti_work)
{
	/* tss_update_io_bitmap, fpregs_assert_state_consistent removed - empty stubs */
	if (unlikely(ti_work & _TIF_NEED_FPU_LOAD))
		switch_fpu_return();
}
#define arch_exit_to_user_mode_prepare arch_exit_to_user_mode_prepare

static __always_inline void arch_exit_to_user_mode(void)
{
	mds_user_clear_cpu_buffers();
}
#define arch_exit_to_user_mode arch_exit_to_user_mode

#endif
