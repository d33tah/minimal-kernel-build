
#include <linux/entry-common.h>
#include <linux/task_work.h>
#include <linux/highmem.h>

noinstr long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall)
{
	local_irq_enable();
	return syscall;
}

static void exit_to_user_mode_prepare(struct pt_regs *regs)
{
	unsigned long ti_work = read_thread_flags();

	while (ti_work & EXIT_TO_USER_MODE_WORK) {
		local_irq_enable();

		if (ti_work & _TIF_NEED_RESCHED)
			schedule();

		if (ti_work & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL))
			arch_do_signal_or_restart(regs);

		if (ti_work & _TIF_NOTIFY_RESUME) {
			clear_thread_flag(TIF_NOTIFY_RESUME);
			smp_mb__after_atomic();
			if (unlikely(task_work_pending(current)))
				task_work_run();
		}

		local_irq_disable_exit_to_user();
		ti_work = read_thread_flags();
	}

	if (unlikely(ti_work & _TIF_NEED_FPU_LOAD))
		switch_fpu_return();
}

__visible noinstr void syscall_exit_to_user_mode(struct pt_regs *regs)
{
	local_irq_disable_exit_to_user();
	exit_to_user_mode_prepare(regs);
}

noinstr irqentry_state_t irqentry_enter(struct pt_regs *regs)
{
	irqentry_state_t ret = {
		.exit_rcu = false,
	};
	return ret;
}

noinstr void irqentry_exit(struct pt_regs *regs, irqentry_state_t state)
{
	if (user_mode(regs)) {
		exit_to_user_mode_prepare(regs);
	}
}

irqentry_state_t noinstr irqentry_nmi_enter(struct pt_regs *regs)
{
	irqentry_state_t irq_state = {};
	__nmi_enter();
	return irq_state;
}

void noinstr irqentry_nmi_exit(struct pt_regs *regs, irqentry_state_t irq_state)
{
	__nmi_exit();
}
