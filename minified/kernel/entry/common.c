
#include <linux/entry-common.h>
#include <linux/hardirq.h>

static __always_inline void __enter_from_user_mode(struct pt_regs *regs)
{
	arch_enter_from_user_mode(regs);
}

noinstr long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall)
{
	__enter_from_user_mode(regs);

	local_irq_enable();

	return syscall;
}

static __always_inline void __exit_to_user_mode(void)
{
	arch_exit_to_user_mode();
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

	lockdep_assert_irqs_disabled();

	if (unlikely(ti_work & EXIT_TO_USER_MODE_WORK))
		ti_work = exit_to_user_mode_loop(regs, ti_work);

	addr_limit_user_check();
	lockdep_assert_irqs_disabled();
	lockdep_sys_exit();
}

__visible noinstr void syscall_exit_to_user_mode(struct pt_regs *regs)
{
	local_irq_disable_exit_to_user();
	exit_to_user_mode_prepare(regs);
	__exit_to_user_mode();
}

noinstr void irqentry_enter_from_user_mode(struct pt_regs *regs)
{
	__enter_from_user_mode(regs);
}

noinstr void irqentry_exit_to_user_mode(struct pt_regs *regs)
{
	exit_to_user_mode_prepare(regs);
	__exit_to_user_mode();
}

noinstr irqentry_state_t irqentry_enter(struct pt_regs *regs)
{
	irqentry_state_t ret = {
		.exit_rcu = false,
	};

	if (user_mode(regs)) {
		irqentry_enter_from_user_mode(regs);
		return ret;
	}

	return ret;
}

noinstr void irqentry_exit(struct pt_regs *regs, irqentry_state_t state)
{
	lockdep_assert_irqs_disabled();

	if (user_mode(regs)) {
		irqentry_exit_to_user_mode(regs);
	}
}

irqentry_state_t noinstr irqentry_nmi_enter(struct pt_regs *regs)
{
	irqentry_state_t irq_state;

	irq_state.lockdep = lockdep_hardirqs_enabled();

	__nmi_enter();
	lockdep_hardirq_enter();

	return irq_state;
}

void noinstr irqentry_nmi_exit(struct pt_regs *regs, irqentry_state_t irq_state)
{
	lockdep_hardirq_exit();
	__nmi_exit();
}
