
#include <linux/entry-common.h>
#include <linux/task_work.h>
#include <linux/highmem.h>
#include <linux/jump_label.h>
#include <linux/init_task.h>
#include <linux/tick.h>
#include <linux/tracepoint.h>

bool syscall_user_dispatch(struct pt_regs *regs);

static __always_inline void __enter_from_user_mode(struct pt_regs *regs)
{
	arch_enter_from_user_mode(regs);
	/* lockdep_hardirqs_off is empty, CT_WARN_ON is dead - context_tracking_enabled always false */
}

void noinstr enter_from_user_mode(struct pt_regs *regs)
{
	__enter_from_user_mode(regs);
}

/* syscall_enter_audit, syscall_trace_enter inlined */

static __always_inline long __syscall_enter_from_user_work(struct pt_regs *regs,
							   long syscall)
{
	unsigned long work = READ_ONCE(current_thread_info()->syscall_work);

	if (work & SYSCALL_WORK_ENTER) {
		/* Inlined syscall_trace_enter */
		if (work & SYSCALL_WORK_SYSCALL_USER_DISPATCH) {
			if (syscall_user_dispatch(regs))
				return -1L;
		}

		if (work &
		    (SYSCALL_WORK_SYSCALL_TRACE | SYSCALL_WORK_SYSCALL_EMU)) {
			if (ptrace_report_syscall_entry(regs) ||
			    (work & SYSCALL_WORK_SYSCALL_EMU))
				return -1L;
		}

		syscall = syscall_get_nr(current, regs);
	}

	return syscall;
}

long syscall_enter_from_user_mode_work(struct pt_regs *regs, long syscall)
{
	return __syscall_enter_from_user_work(regs, syscall);
}

noinstr long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall)
{
	long ret;

	__enter_from_user_mode(regs);

	local_irq_enable();
	ret = __syscall_enter_from_user_work(regs, syscall);

	return ret;
}

noinstr void syscall_enter_from_user_mode_prepare(struct pt_regs *regs)
{
	__enter_from_user_mode(regs);
	local_irq_enable();
}

static __always_inline void __exit_to_user_mode(void)
{
	arch_exit_to_user_mode();
}

void noinstr exit_to_user_mode(void)
{
	__exit_to_user_mode();
}

/* arch_do_signal_or_restart - x86 provides its own in arch/x86/kernel/signal.c */

static unsigned long exit_to_user_mode_loop(struct pt_regs *regs,
					    unsigned long ti_work)
{
	while (ti_work & EXIT_TO_USER_MODE_WORK) {
		local_irq_enable(); /* local_irq_enable_exit_to_user inlined */

		if (ti_work & _TIF_NEED_RESCHED)
			schedule();

		if (ti_work & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL))
			arch_do_signal_or_restart(regs);

		if (ti_work & _TIF_NOTIFY_RESUME) {
			/* resume_user_mode_work inlined */
			clear_thread_flag(TIF_NOTIFY_RESUME);
			smp_mb__after_atomic();
			if (unlikely(task_work_pending(current)))
				task_work_run();
		}

		/* arch_exit_to_user_mode_work is empty on x86 */

		local_irq_disable_exit_to_user();

		/* tick_nohz_user_enter_prepare is empty stub */
		ti_work = read_thread_flags();
	}

	return ti_work;
}

static void exit_to_user_mode_prepare(struct pt_regs *regs)
{
	unsigned long ti_work = read_thread_flags();

	/* tick_nohz_user_enter_prepare is empty stub */

	if (unlikely(ti_work & EXIT_TO_USER_MODE_WORK))
		ti_work = exit_to_user_mode_loop(regs, ti_work);

	/* arch_exit_to_user_mode_prepare inlined */
	if (unlikely(ti_work & _TIF_NEED_FPU_LOAD))
		switch_fpu_return();

	/* addr_limit_user_check removed - empty stub */
	/* kmap_assert_nomap inlined */
	DEBUG_LOCKS_WARN_ON(current->kmap_ctrl.idx);
	/* lockdep_sys_exit is empty do{}while(0) */
}

/* report_single_step inlined */
/* syscall_exit_to_user_mode_prepare inlined */

static __always_inline void
__syscall_exit_to_user_mode_work(struct pt_regs *regs)
{
	unsigned long work = READ_ONCE(current_thread_info()->syscall_work);
	if (unlikely(work & SYSCALL_WORK_EXIT)) {
		bool step;

		if (work & SYSCALL_WORK_SYSCALL_USER_DISPATCH) {
			if (unlikely(current->syscall_dispatch.on_dispatch)) {
				current->syscall_dispatch.on_dispatch = false;
				goto done_exit_work;
			}
		}

		step = !(work & SYSCALL_WORK_SYSCALL_EMU) &&
		       (work & SYSCALL_WORK_SYSCALL_EXIT_TRAP);
		if (step || work & SYSCALL_WORK_SYSCALL_TRACE)
			ptrace_report_syscall_exit(regs, step);
	}
done_exit_work:
	local_irq_disable_exit_to_user();
	exit_to_user_mode_prepare(regs);
}

void syscall_exit_to_user_mode_work(struct pt_regs *regs)
{
	__syscall_exit_to_user_mode_work(regs);
}

__visible noinstr void syscall_exit_to_user_mode(struct pt_regs *regs)
{
	__syscall_exit_to_user_mode_work(regs);
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

	/* TINY_RCU, lockdep disabled - simplified path */
	return ret;
}

void raw_irqentry_exit_cond_resched(void)
{
	if (!preempt_count()) {
		/* rcu_irq_exit_check_preempt is empty stub */
		if (need_resched())
			preempt_schedule_irq();
	}
}

noinstr void irqentry_exit(struct pt_regs *regs, irqentry_state_t state)
{
	if (user_mode(regs)) {
		irqentry_exit_to_user_mode(regs);
	}
	/* rcu_irq_exit is empty - state.exit_rcu branches removed */
}

irqentry_state_t noinstr irqentry_nmi_enter(struct pt_regs *regs)
{
	irqentry_state_t irq_state = {};
	/* lockdep_hardirqs_enabled always 0, lockdep_hardirqs_off is empty */
	__nmi_enter();
	return irq_state;
}

void noinstr irqentry_nmi_exit(struct pt_regs *regs, irqentry_state_t irq_state)
{
	__nmi_exit();
}
