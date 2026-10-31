#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

/* Inlined from irqreturn.h */
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

struct irqaction {
	irq_handler_t handler;
	void *dev_id;
};

extern irqreturn_t no_action(int cpl, void *dev_id);

extern int __must_check request_irq(unsigned int irq, irq_handler_t handler,
				    void *dev_id);

extern void early_irq_init(void);

#endif
