#include <linux/entry-common.h>

void arch_do_signal_or_restart(struct pt_regs *regs)
{
	restore_saved_sigmask();
}
