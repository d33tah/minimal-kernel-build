/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ENTRYCOMMON_H
#define __LINUX_ENTRYCOMMON_H

#include <linux/syscalls.h>

#define EXIT_TO_USER_MODE_WORK (_TIF_NEED_RESCHED)

/**
 * syscall_enter_from_user_mode - Establish state and check and handle work
 *				  before invoking a syscall
 * @regs:	Pointer to currents pt_regs
 * @syscall:	The syscall number
 *
 * Invoked from architecture specific syscall entry code with interrupts
 * disabled. The calling code has to be non-instrumentable. When the
 * function returns all state is correct, interrupts are enabled and the
 * subsequent functions can be instrumented.
 *
 * This is combination of syscall_enter_from_user_mode_prepare() and
 * syscall_enter_from_user_mode_work().
 *
 * Returns: The original or a modified syscall number. See
 * syscall_enter_from_user_mode_work() for further explanation.
 */
long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall);

/**
 * local_irq_enable_exit_to_user - Exit to user variant of local_irq_enable()
 * @ti_work:	Cached TIF flags gathered with interrupts disabled
 *
 * Defaults to local_irq_enable(). Can be supplied by architecture specific
 * code.
 */
static inline void local_irq_enable_exit_to_user(unsigned long ti_work);

static inline void local_irq_enable_exit_to_user(unsigned long ti_work)
{
	local_irq_enable();
}

/**
 * local_irq_disable_exit_to_user - Exit to user variant of local_irq_disable()
 *
 * Defaults to local_irq_disable(). Can be supplied by architecture specific
 * code.
 */
static inline void local_irq_disable_exit_to_user(void);

static inline void local_irq_disable_exit_to_user(void)
{
	local_irq_disable();
}

/**
 * syscall_exit_to_user_mode - Handle work before returning to user mode
 * @regs:	Pointer to currents pt_regs
 *
 * Invoked with interrupts enabled and fully valid regs. Returns with all
 * work handled, interrupts disabled such that the caller can immediately
 * switch to user mode. Called from architecture specific syscall and ret
 * from fork code.
 *
 * The call order is:
 *  1) One-time syscall exit work:
 *	- rseq syscall exit
 *      - audit
 *	- syscall tracing
 *	- ptrace (single stepping)
 *
 *  2) Preparatory work
 *	- Exit to user mode loop (common TIF handling). Invokes
 *	  arch_exit_to_user_mode_work() for architecture specific TIF work
 *	- Architecture specific one time work arch_exit_to_user_mode_prepare()
 *	- Address limit and lockdep checks
 *
 *  3) Final transition (lockdep, tracing, context tracking, RCU), i.e. the
 *     functionality in exit_to_user_mode().
 *
 * This is a combination of syscall_exit_to_user_mode_work() (1,2) and
 * exit_to_user_mode(). This function is preferred unless there is a
 * compelling architectural reason to use the separate functions.
 */
void syscall_exit_to_user_mode(struct pt_regs *regs);

/**
 * irqentry_exit_to_user_mode - Interrupt exit work
 * @regs:	Pointer to current's pt_regs
 *
 * Invoked with interrupts disabled and fully valid regs. Returns with all
 * work handled, interrupts disabled such that the caller can immediately
 * switch to user mode. Called from architecture specific interrupt
 * handling code.
 *
 * The call order is #2 and #3 as described in syscall_exit_to_user_mode().
 * Interrupt exit is not invoking #1 which is the syscall specific one time
 * work.
 */
void irqentry_exit_to_user_mode(struct pt_regs *regs);

/**
 * struct irqentry_state - Opaque object for exception state storage
 * @exit_rcu: Used exclusively in the irqentry_*() calls; signals whether the
 *            exit path has to invoke rcu_irq_exit().
 * @lockdep: Used exclusively in the irqentry_nmi_*() calls; ensures that
 *           lockdep state is restored correctly on exit from nmi.
 *
 * This opaque object is filled in by the irqentry_*_enter() functions and
 * must be passed back into the corresponding irqentry_*_exit() functions
 * when the exception is complete.
 *
 * Callers of irqentry_*_[enter|exit]() must consider this structure opaque
 * and all members private.  Descriptions of the members are provided to aid in
 * the maintenance of the irqentry_*() functions.
 */
typedef struct irqentry_state {
} irqentry_state_t;

/**
 * irqentry_enter - Handle state tracking on ordinary interrupt entries
 * @regs:	Pointer to pt_regs of interrupted context
 *
 * Invokes:
 *  - lockdep irqflag state tracking as low level ASM entry disabled
 *    interrupts.
 *
 *  - Context tracking if the exception hit user mode.
 *
 *  - The hardirq tracer to keep the state consistent as low level ASM
 *    entry disabled interrupts.
 *
 * As a precondition, this requires that the entry came from user mode,
 * idle, or a kernel context in which RCU is watching.
 *
 * For kernel mode entries RCU handling is done conditional. If RCU is
 * watching then the only RCU requirement is to check whether the tick has
 * to be restarted. If RCU is not watching then rcu_irq_enter() has to be
 * invoked on entry and rcu_irq_exit() on exit.
 *
 * Avoiding the rcu_irq_enter/exit() calls is an optimization but also
 * solves the problem of kernel mode pagefaults which can schedule, which
 * is not possible after invoking rcu_irq_enter() without undoing it.
 *
 * For user mode entries irqentry_enter_from_user_mode() is invoked to
 * establish the proper context for NOHZ_FULL. Otherwise scheduling on exit
 * would not be possible.
 *
 * Returns: An opaque object that must be passed to idtentry_exit()
 */
irqentry_state_t noinstr irqentry_enter(struct pt_regs *regs);

/**
 * irqentry_exit - Handle return from exception that used irqentry_enter()
 * @regs:	Pointer to pt_regs (exception entry regs)
 * @state:	Return value from matching call to irqentry_enter()
 *
 * Depending on the return target (kernel/user) this runs the necessary
 * preemption and work checks if possible and required and returns to
 * the caller with interrupts disabled and no further work pending.
 *
 * This is the last action before returning to the low level ASM code which
 * just needs to return to the appropriate context.
 *
 * Counterpart to irqentry_enter().
 */
void noinstr irqentry_exit(struct pt_regs *regs, irqentry_state_t state);

/**
 * irqentry_nmi_enter - Handle NMI entry
 * @regs:	Pointer to currents pt_regs
 *
 * Similar to irqentry_enter() but taking care of the NMI constraints.
 */
irqentry_state_t noinstr irqentry_nmi_enter(struct pt_regs *regs);

void noinstr irqentry_nmi_exit(struct pt_regs *regs,
			       irqentry_state_t irq_state);

#endif
