#ifndef _LINUX_IRQDESC_H
#define _LINUX_IRQDESC_H

struct irq_desc {
	struct irq_data irq_data;
	irq_flow_handler_t handle_irq;
	struct irqaction *action;
	raw_spinlock_t lock;
} ____cacheline_internodealigned_in_smp;

extern struct irq_desc *irq_to_desc(unsigned int irq);

static inline void generic_handle_irq_desc(struct irq_desc *desc)
{
	desc->handle_irq(desc);
}

#endif
