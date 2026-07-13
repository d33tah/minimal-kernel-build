#ifndef _LINUX_IRQNR_H
#define _LINUX_IRQNR_H




extern int nr_irqs;
extern struct irq_desc *irq_to_desc(unsigned int irq);

#endif
