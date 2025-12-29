#ifndef LINUX_HARDIRQ_H
#define LINUX_HARDIRQ_H

#include <linux/preempt.h>
#include <linux/lockdep.h>
#include <linux/sched.h>
#include <linux/vtime.h>
#include <asm/hardirq.h>

#define __irq_enter()	preempt_count_add(HARDIRQ_OFFSET)
#define __irq_enter_raw() __irq_enter()

void irq_enter(void);
void irq_enter_rcu(void);

#define __irq_exit()	preempt_count_sub(HARDIRQ_OFFSET)
#define __irq_exit_raw() __irq_exit()

void irq_exit(void);
void irq_exit_rcu(void);

/* arch_nmi_enter/exit, rcu_nmi_enter/exit removed - empty stubs */

#define __nmi_enter()						\
	do {							\
		lockdep_off();					\
		BUG_ON(in_nmi() == NMI_MASK);			\
		__preempt_count_add(NMI_OFFSET + HARDIRQ_OFFSET);	\
	} while (0)

#define nmi_enter()	__nmi_enter()

#define __nmi_exit()						\
	do {							\
		BUG_ON(!in_nmi());				\
		__preempt_count_sub(NMI_OFFSET + HARDIRQ_OFFSET);	\
		lockdep_on();					\
	} while (0)

#define nmi_exit()	__nmi_exit()

#endif  
