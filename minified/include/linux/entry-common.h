#ifndef __LINUX_ENTRYCOMMON_H
#define __LINUX_ENTRYCOMMON_H

#include <linux/ptrace.h>
#include <linux/syscalls.h>

#include <linux/sched/task_stack.h>
#include <asm/nospec-branch.h>
#include <asm/fpu/api.h>

#ifndef _TIF_PATCH_PENDING
# define _TIF_PATCH_PENDING		(0)
#endif

#ifndef _TIF_UPROBE
# define _TIF_UPROBE			(0)
#endif

#ifndef ARCH_EXIT_TO_USER_MODE_WORK
# define ARCH_EXIT_TO_USER_MODE_WORK		(0)
#endif

#define EXIT_TO_USER_MODE_WORK						\
	(_TIF_SIGPENDING | _TIF_NOTIFY_RESUME | _TIF_UPROBE |		\
	 _TIF_NEED_RESCHED | _TIF_PATCH_PENDING | _TIF_NOTIFY_SIGNAL |	\
	 ARCH_EXIT_TO_USER_MODE_WORK)

long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall);

static inline void local_irq_disable_exit_to_user(void);

#ifndef local_irq_disable_exit_to_user
static inline void local_irq_disable_exit_to_user(void)
{
	local_irq_disable();
}
#endif

static inline void arch_do_signal_or_restart(struct pt_regs *regs)
{
}

void syscall_exit_to_user_mode(struct pt_regs *regs);

#ifndef irqentry_state
typedef struct irqentry_state {
	bool	exit_rcu;
} irqentry_state_t;
#endif

irqentry_state_t noinstr irqentry_enter(struct pt_regs *regs);
void noinstr irqentry_exit(struct pt_regs *regs, irqentry_state_t state);

irqentry_state_t noinstr irqentry_nmi_enter(struct pt_regs *regs);

void noinstr irqentry_nmi_exit(struct pt_regs *regs, irqentry_state_t irq_state);

#endif
