
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <asm/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <linux/uaccess.h>
#include <linux/entry-common.h>
#include <linux/syscalls.h>

#include <asm/processor.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/xstate.h>
#include <asm/vdso.h>
#include <asm/syscall.h>
#include <asm/sigframe.h>
#include <asm/signal.h>

/* signal_fault, __unsafe_setup_sigcontext, unsafe_put_sigcontext,
 * unsafe_put_sigmask, align_sigframe, get_sigframe, retcode, rt_retcode,
 * __setup_frame, __setup_rt_frame removed - get_signal returns false */

#define FRAME_ALIGNMENT 16UL
#define MAX_FRAME_PADDING (FRAME_ALIGNMENT - 1)

#define MAX_FRAME_SIGINFO_UCTXT_SIZE sizeof(struct sigframe_ia32)

#define MAX_XSAVE_PADDING 63UL

static unsigned long __ro_after_init max_frame_size;
static unsigned int __ro_after_init fpu_default_state_size;

void __init init_sigframe_size(void)
{
	fpu_default_state_size = fpu__get_fpstate_size();

	max_frame_size = MAX_FRAME_SIGINFO_UCTXT_SIZE + MAX_FRAME_PADDING;

	max_frame_size += fpu_default_state_size + MAX_XSAVE_PADDING;

	max_frame_size = round_up(max_frame_size, FRAME_ALIGNMENT);

	pr_info("max sigframe size: %lu\n", max_frame_size);
}

void arch_do_signal_or_restart(struct pt_regs *regs)
{
	if (syscall_get_nr(current, regs) != -1) {
		switch (syscall_get_error(current, regs)) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->ax = regs->orig_ax;
			regs->ip -= 2;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->ax = __NR_restart_syscall;
			regs->ip -= 2;
			break;
		}
	}

	restore_saved_sigmask();
}
