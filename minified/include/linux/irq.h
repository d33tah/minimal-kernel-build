#ifndef _LINUX_IRQ_H
#define _LINUX_IRQ_H

struct irq_desc;
typedef void (*irq_flow_handler_t)(struct irq_desc *desc);
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <asm/irq_regs.h>

struct irq_data {
	unsigned int irq;
	struct irq_chip *chip;
};

struct irq_chip {
	const char *name;
	void (*irq_mask_ack)(struct irq_data *data);
	void (*irq_unmask)(struct irq_data *data);
};

#include <linux/irqdesc.h>

#include <asm/hw_irq.h>

extern void handle_level_irq(struct irq_desc *desc);
extern void handle_bad_irq(struct irq_desc *desc);
extern struct irq_chip dummy_irq_chip;

extern void irq_set_chip_and_handler(unsigned int irq, struct irq_chip *chip,
				     irq_flow_handler_t handle);

#endif
