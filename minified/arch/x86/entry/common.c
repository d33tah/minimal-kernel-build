 
 

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

static noinstr bool __do_fast_syscall_32(struct pt_regs *regs)
{
	int nr = syscall_32_enter(regs);
	int res;

	 
	syscall_enter_from_user_mode_prepare(regs);

	 
	if (IS_ENABLED(CONFIG_X86_64)) {
		 
		res = __get_user(*(u32 *)&regs->bp,
			 (u32 __user __force *)(unsigned long)(u32)regs->sp);
	} else {
		res = get_user(*(u32 *)&regs->bp,
		       (u32 __user __force *)(unsigned long)(u32)regs->sp);
	}

	if (res) {
		 
		regs->ax = -EFAULT;

		local_irq_disable();
		irqentry_exit_to_user_mode(regs);
		return false;
	}

	nr = syscall_enter_from_user_mode_work(regs, nr);

	 
	do_syscall_32_irqs_on(regs, nr);

	syscall_exit_to_user_mode(regs);
	return true;
}

 
__visible noinstr long do_fast_syscall_32(struct pt_regs *regs)
{
	 
	unsigned long landing_pad = (unsigned long)current->mm->context.vdso +
					vdso_image_32.sym_int80_landing_pad;

	 
	regs->ip = landing_pad;

	 
	if (!__do_fast_syscall_32(regs))
		return 0;

	 
	return static_cpu_has(X86_FEATURE_SEP) &&
		regs->cs == __USER_CS && regs->ss == __USER_DS &&
		regs->ip == landing_pad &&
		(regs->flags & (X86_EFLAGS_RF | X86_EFLAGS_TF | X86_EFLAGS_VM)) == 0;
}

 
__visible noinstr long do_SYSENTER_32(struct pt_regs *regs)
{
	 
	regs->sp = regs->bp;

	 
	regs->flags |= X86_EFLAGS_IF;

	return do_fast_syscall_32(regs);
}

SYSCALL_DEFINE0(ni_syscall)
{
	return -ENOSYS;
}

