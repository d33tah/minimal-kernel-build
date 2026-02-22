
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/entry-common.h>

#include <asm/fpu/signal.h>
#include <asm/sigframe.h>

void __init init_sigframe_size(void)
{
}

void arch_do_signal_or_restart(struct pt_regs *regs)
{
	restore_saved_sigmask();
}
