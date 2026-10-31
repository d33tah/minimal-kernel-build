
#include <linux/entry-common.h>
#include <linux/hardirq.h>

noinstr long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall)
{
	local_irq_enable();

	return syscall;
}

static unsigned long exit_to_user_mode_loop(struct pt_regs *regs,
					    unsigned long ti_work)
{
	while (ti_work & EXIT_TO_USER_MODE_WORK) {
		local_irq_enable_exit_to_user(ti_work);

		if (ti_work & _TIF_NEED_RESCHED)
			schedule();

		local_irq_disable_exit_to_user();

		ti_work = read_thread_flags();
	}

	return ti_work;
}

static void exit_to_user_mode_prepare(struct pt_regs *regs)
{
	unsigned long ti_work = read_thread_flags();

	if (unlikely(ti_work & EXIT_TO_USER_MODE_WORK))
		ti_work = exit_to_user_mode_loop(regs, ti_work);
}

__visible noinstr void syscall_exit_to_user_mode(struct pt_regs *regs)
{
	local_irq_disable_exit_to_user();
	exit_to_user_mode_prepare(regs);
}

noinstr void irqentry_exit_to_user_mode(struct pt_regs *regs)
{
	exit_to_user_mode_prepare(regs);
}

noinstr irqentry_state_t irqentry_enter(struct pt_regs *regs)
{
	irqentry_state_t ret = {};

	return ret;
}

noinstr void irqentry_exit(struct pt_regs *regs, irqentry_state_t state)
{
	if (user_mode(regs)) {
		irqentry_exit_to_user_mode(regs);
	}
}

irqentry_state_t noinstr irqentry_nmi_enter(struct pt_regs *regs)
{
	irqentry_state_t irq_state;

	__nmi_enter();

	return irq_state;
}

void noinstr irqentry_nmi_exit(struct pt_regs *regs, irqentry_state_t irq_state)
{
	__nmi_exit();
}
