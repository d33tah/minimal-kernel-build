#ifndef LINUX_HARDIRQ_H
#define LINUX_HARDIRQ_H

#include <linux/preempt.h>
#include <linux/lockdep.h>
#include <linux/sched.h>
#include <asm/hardirq.h>

#define __irq_enter()	preempt_count_add(HARDIRQ_OFFSET)
#define __irq_enter_raw() __irq_enter()

void irq_enter_rcu(void);

void irq_exit_rcu(void);

#define __nmi_enter()						\
	do {							\
		/* lockdep_off() removed - empty stub */	\
		BUG_ON(in_nmi() == NMI_MASK);			\
		__preempt_count_add(NMI_OFFSET + HARDIRQ_OFFSET);	\
	} while (0)

#define __nmi_exit()						\
	do {							\
		BUG_ON(!in_nmi());				\
		__preempt_count_sub(NMI_OFFSET + HARDIRQ_OFFSET);	\
		/* lockdep_on() removed - empty stub */		\
	} while (0)

#endif  
