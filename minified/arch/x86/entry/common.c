// SPDX-License-Identifier: GPL-2.0-only
/*
 * common.c - C code for kernel entry and exit
 * Copyright (c) 2015 Andrew Lutomirski
 *
 * Based on asm and ptrace code by many authors.  The code here originated
 * in ptrace.c and signal.c.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/entry-common.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/export.h>
#include <linux/nospec.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>


#include <asm/desc.h>
#include <asm/traps.h>
#include <asm/vdso.h>
#include <asm/cpufeature.h>
#include <asm/fpu/api.h>
#include <asm/nospec-branch.h>
#include <asm/io_bitmap.h>
#include <asm/syscall.h>
#include <asm/irq_stack.h>


static __always_inline int syscall_32_enter(struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_IA32_EMULATION))
		current_thread_info()->status |= TS_COMPAT;

	return (int)regs->orig_ax;
}

/*
 * Invoke a 32-bit syscall.  Called with IRQs on in CONTEXT_KERNEL.
 */
static __always_inline void do_syscall_32_irqs_on(struct pt_regs *regs, int nr)
{
	/*
	 * Convert negative numbers to very high and thus out of range
	 * numbers for comparisons.
	 */
	unsigned int unr = nr;

	if (likely(unr < IA32_NR_syscalls)) {
		unr = array_index_nospec(unr, IA32_NR_syscalls);
		regs->ax = ia32_sys_call_table[unr](regs);
	} else if (nr != -1) {
		regs->ax = __ia32_sys_ni_syscall(regs);
	}
}

/* Handles int $0x80 */
__visible noinstr void do_int80_syscall_32(struct pt_regs *regs)
{
	int nr = syscall_32_enter(regs);

	/*
	 * Subtlety here: if ptrace pokes something larger than 2^31-1 into
	 * orig_ax, the int return value truncates it. This matches
	 * the semantics of syscall_get_nr().
	 */
	nr = syscall_enter_from_user_mode(regs, nr);

	do_syscall_32_irqs_on(regs, nr);

	syscall_exit_to_user_mode(regs);
}

static noinstr bool __do_fast_syscall_32(struct pt_regs *regs)
{
	int nr = syscall_32_enter(regs);
	int res;

	/*
	 * This cannot use syscall_enter_from_user_mode() as it has to
	 * fetch EBP before invoking any of the syscall entry work
	 * functions.
	 */
	syscall_enter_from_user_mode_prepare(regs);

	/* Fetch EBP from where the vDSO stashed it. */
	if (IS_ENABLED(CONFIG_X86_64)) {
		/*
		 * Micro-optimization: the pointer we're following is
		 * explicitly 32 bits, so it can't be out of range.
		 */
		res = __get_user(*(u32 *)&regs->bp,
			 (u32 __user __force *)(unsigned long)(u32)regs->sp);
	} else {
		res = get_user(*(u32 *)&regs->bp,
		       (u32 __user __force *)(unsigned long)(u32)regs->sp);
	}

	if (res) {
		/* User code screwed up. */
		regs->ax = -EFAULT;

		local_irq_disable();
		irqentry_exit_to_user_mode(regs);
		return false;
	}

	nr = syscall_enter_from_user_mode_work(regs, nr);

	/* Now this is just like a normal syscall. */
	do_syscall_32_irqs_on(regs, nr);

	syscall_exit_to_user_mode(regs);
	return true;
}

/* Returns 0 to return using IRET or 1 to return using SYSEXIT/SYSRETL. */
__visible noinstr long do_fast_syscall_32(struct pt_regs *regs)
{
	/*
	 * Called using the internal vDSO SYSENTER/SYSCALL32 calling
	 * convention.  Adjust regs so it looks like we entered using int80.
	 */
	unsigned long landing_pad = (unsigned long)current->mm->context.vdso +
					vdso_image_32.sym_int80_landing_pad;

	/*
	 * SYSENTER loses EIP, and even SYSCALL32 needs us to skip forward
	 * so that 'regs->ip -= 2' lands back on an int $0x80 instruction.
	 * Fix it up.
	 */
	regs->ip = landing_pad;

	/* Invoke the syscall. If it failed, keep it simple: use IRET. */
	if (!__do_fast_syscall_32(regs))
		return 0;

	/*
	 * Opportunistic SYSEXIT: if possible, try to return using SYSEXIT.
	 *
	 * Unlike 64-bit opportunistic SYSRET, we can't check that CX == IP,
	 * because the ECX fixup above will ensure that this is essentially
	 * never the case.
	 *
	 * We don't allow syscalls at all from VM86 mode, but we still
	 * need to check VM, because we might be returning from sys_vm86.
	 */
	return static_cpu_has(X86_FEATURE_SEP) &&
		regs->cs == __USER_CS && regs->ss == __USER_DS &&
		regs->ip == landing_pad &&
		(regs->flags & (X86_EFLAGS_RF | X86_EFLAGS_TF | X86_EFLAGS_VM)) == 0;
}

/* Returns 0 to return using IRET or 1 to return using SYSEXIT/SYSRETL. */
__visible noinstr long do_SYSENTER_32(struct pt_regs *regs)
{
	/* SYSENTER loses RSP, but the vDSO saved it in RBP. */
	regs->sp = regs->bp;

	/* SYSENTER clobbers EFLAGS.IF.  Assume it was set in usermode. */
	regs->flags |= X86_EFLAGS_IF;

	return do_fast_syscall_32(regs);
}

SYSCALL_DEFINE0(ni_syscall)
{
	return -ENOSYS;
}

