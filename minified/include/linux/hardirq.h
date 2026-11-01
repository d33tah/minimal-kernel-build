/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_HARDIRQ_H
#define LINUX_HARDIRQ_H

#include <linux/sched.h>
#include <asm/hardirq.h>

/*
 * Like __irq_enter() without time accounting for fast
 * interrupts, e.g. reschedule IPI where time accounting
 * is more expensive than the actual interrupt.
 */
#define __irq_enter_raw()                          \
	do {                                       \
		preempt_count_add(HARDIRQ_OFFSET); \
	} while (0)

/*
 * Like irq_enter(), but RCU is already watching.
 */
void irq_enter_rcu(void);

/*
 * Like irq_exit(), but return with RCU watching.
 */
void irq_exit_rcu(void);

/*
 * NMI vs Tracing
 * --------------
 *
 * We must not land in a tracer until (or after) we've changed preempt_count
 * such that in_nmi() becomes true. To that effect all NMI C entry points must
 * be marked 'notrace' and call nmi_enter() as soon as possible.
 */

/*
 * nmi_enter() can nest up to 15 times; see NMI_BITS.
 */
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

#endif /* LINUX_HARDIRQ_H */
