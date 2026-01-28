#ifndef _LINUX_IRQNR_H
#define _LINUX_IRQNR_H

extern int nr_irqs;
extern struct irq_desc *irq_to_desc(unsigned int irq);
/* irq_get_next_irq removed - only used by for_each_active_irq */
/* for_each_irq_desc, for_each_irq_desc_reverse, for_each_active_irq removed - never invoked */

#endif
