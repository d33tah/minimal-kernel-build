/* SPDX-License-Identifier: GPL-2.0 */
/* interrupt.h */
#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

enum irqreturn {
	IRQ_NONE = (0 << 0),
	IRQ_HANDLED = (1 << 0),
};
typedef enum irqreturn irqreturn_t;

#include <linux/hardirq.h>

#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/sections.h>

typedef irqreturn_t (*irq_handler_t)(int, void *);

/**
 * struct irqaction - per interrupt action descriptor
 * @handler:	interrupt handler function
 * @name:	name of the device
 * @dev_id:	cookie to identify the device
 * @percpu_dev_id:	cookie to identify the device
 * @next:	pointer to the next irqaction for shared interrupts
 * @irq:	interrupt number
 * @flags:	flags (see IRQF_* above)
 * @thread_fn:	interrupt handler function for threaded interrupts
 * @thread:	thread pointer for threaded interrupts
 * @secondary:	pointer to secondary irqaction (force threading)
 * @thread_flags:	flags related to @thread
 * @thread_mask:	bitmask for keeping track of @thread activity
 * @dir:	pointer to the proc/irq/NN/name entry
 */
struct irqaction {
	irq_handler_t handler;
	void *dev_id;
};

extern irqreturn_t no_action(int cpl, void *dev_id);

extern int __must_check request_irq(unsigned int irq, irq_handler_t handler,
				    void *dev_id);

extern void early_irq_init(void);

#endif
