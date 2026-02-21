
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/entry-common.h>

#include <asm/fpu/signal.h>
#include <asm/sigframe.h>

#define FRAME_ALIGNMENT 16UL
#define MAX_FRAME_PADDING (FRAME_ALIGNMENT - 1)

#define MAX_FRAME_SIGINFO_UCTXT_SIZE sizeof(struct sigframe_ia32)

void __init init_sigframe_size(void)
{
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
			panic("ERESTART_RESTARTBLOCK");
			break;
		}
	}

	restore_saved_sigmask();
}
