
#ifndef _ASM_X86_IRQ_H
#define _ASM_X86_IRQ_H

/* irq_canonicalize removed - unused */

extern int irq_init_percpu_irqstack(unsigned int cpu);

struct irq_desc;
extern void native_init_IRQ(void);

extern void __handle_irq(struct irq_desc *desc, struct pt_regs *regs);

extern void __init init_IRQ(void);

#endif
