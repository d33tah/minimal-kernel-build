
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/entry-common.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/nospec.h>
#include <linux/uaccess.h>


#include <asm/vdso.h>
#include <asm/cpufeature.h>
#include <asm/syscall.h>


static __always_inline int syscall_32_enter(struct pt_regs *regs)
{
	return (int)regs->orig_ax;
}

static __always_inline void do_syscall_32_irqs_on(struct pt_regs *regs, int nr)
{
	 
	unsigned int unr = nr;

	if (likely(unr < IA32_NR_syscalls)) {
		unr = array_index_nospec(unr, IA32_NR_syscalls);
		regs->ax = ia32_sys_call_table[unr](regs);
	} else if (nr != -1) {
		regs->ax = __ia32_sys_ni_syscall(regs);
	}
}

__visible noinstr void do_int80_syscall_32(struct pt_regs *regs)
{
	int nr = syscall_32_enter(regs);

	 
	nr = syscall_enter_from_user_mode(regs, nr);

	do_syscall_32_irqs_on(regs, nr);

	syscall_exit_to_user_mode(regs);
}

__visible noinstr long do_SYSENTER_32(struct pt_regs *regs)
{
	/*
	 * Runtime-dead in this minimal build: userspace enters via int 0x80
	 * (do_int80_syscall_32), never via SYSENTER. Body stubbed; symbol
	 * retained for the (never-executed) asm SYSENTER entry stub.
	 */
	return 0;
}

SYSCALL_DEFINE0(ni_syscall)
{
	return -ENOSYS;
}

