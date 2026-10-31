#ifndef __LINUX_ENTRYCOMMON_H
#define __LINUX_ENTRYCOMMON_H

#include <linux/syscalls.h>

#define EXIT_TO_USER_MODE_WORK (_TIF_NEED_RESCHED)

long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall);

static inline void local_irq_enable_exit_to_user(unsigned long ti_work);

static inline void local_irq_enable_exit_to_user(unsigned long ti_work)
{
	local_irq_enable();
}

static inline void local_irq_disable_exit_to_user(void);

static inline void local_irq_disable_exit_to_user(void)
{
	local_irq_disable();
}

void syscall_exit_to_user_mode(struct pt_regs *regs);

void irqentry_exit_to_user_mode(struct pt_regs *regs);

typedef struct irqentry_state {
	union {
		bool exit_rcu, lockdep;
	};
} irqentry_state_t;

irqentry_state_t noinstr irqentry_enter(struct pt_regs *regs);

void noinstr irqentry_exit(struct pt_regs *regs, irqentry_state_t state);

irqentry_state_t noinstr irqentry_nmi_enter(struct pt_regs *regs);

void noinstr irqentry_nmi_exit(struct pt_regs *regs,
			       irqentry_state_t irq_state);

#endif
