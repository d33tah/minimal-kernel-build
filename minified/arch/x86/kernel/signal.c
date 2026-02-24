#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/entry-common.h>

#include <asm/fpu/signal.h>

void arch_do_signal_or_restart(struct pt_regs *regs)
{
	restore_saved_sigmask();
}
