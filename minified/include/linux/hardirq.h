#ifndef LINUX_HARDIRQ_H
#define LINUX_HARDIRQ_H

#include <linux/sched.h>
#include <asm/hardirq.h>

#define __irq_enter_raw()                          \
	do {                                       \
		preempt_count_add(HARDIRQ_OFFSET); \
	} while (0)

void irq_enter_rcu(void);

void irq_exit_rcu(void);

#define __nmi_enter()                                             \
	do {                                                      \
		BUG_ON(in_nmi() == NMI_MASK);                     \
		__preempt_count_add(NMI_OFFSET + HARDIRQ_OFFSET); \
	} while (0)

#define __nmi_exit()                                              \
	do {                                                      \
		BUG_ON(!in_nmi());                                \
		__preempt_count_sub(NMI_OFFSET + HARDIRQ_OFFSET); \
	} while (0)

#endif
