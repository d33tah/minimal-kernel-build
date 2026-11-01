#ifndef _ASM_X86_IRQ_H
#define _ASM_X86_IRQ_H

extern int irq_init_percpu_irqstack(unsigned int cpu);

struct irq_desc;

extern void __handle_irq(struct irq_desc *desc, struct pt_regs *regs);

extern void __init init_IRQ(void);

#endif
