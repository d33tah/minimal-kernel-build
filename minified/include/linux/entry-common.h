#ifndef __LINUX_ENTRYCOMMON_H
#define __LINUX_ENTRYCOMMON_H

#include <linux/ptrace.h>
#include <linux/syscalls.h>

#include <asm/entry-common.h>

#define EXIT_TO_USER_MODE_WORK (_TIF_NEED_RESCHED)

static __always_inline void arch_enter_from_user_mode(struct pt_regs *regs);

long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall);

static inline void local_irq_enable_exit_to_user(unsigned long ti_work);

#ifndef local_irq_enable_exit_to_user
static inline void local_irq_enable_exit_to_user(unsigned long ti_work)
{
	local_irq_enable();
}
#endif

static inline void local_irq_disable_exit_to_user(void);

#ifndef local_irq_disable_exit_to_user
static inline void local_irq_disable_exit_to_user(void)
{
	local_irq_disable();
}
#endif

static __always_inline void arch_exit_to_user_mode(void);

void syscall_exit_to_user_mode(struct pt_regs *regs);

void irqentry_enter_from_user_mode(struct pt_regs *regs);

void irqentry_exit_to_user_mode(struct pt_regs *regs);

#ifndef irqentry_state
typedef struct irqentry_state {
	union {
		bool exit_rcu, lockdep;
	};
} irqentry_state_t;
#endif

irqentry_state_t noinstr irqentry_enter(struct pt_regs *regs);

void noinstr irqentry_exit(struct pt_regs *regs, irqentry_state_t state);

irqentry_state_t noinstr irqentry_nmi_enter(struct pt_regs *regs);

void noinstr irqentry_nmi_exit(struct pt_regs *regs,
			       irqentry_state_t irq_state);

#endif
