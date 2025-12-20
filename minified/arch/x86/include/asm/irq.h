 
#ifndef _ASM_X86_IRQ_H
#define _ASM_X86_IRQ_H
 

#include <asm/apicdef.h>
#include <asm/irq_vectors.h>

 
#define __irq_entry __invalid_section

/* irq_canonicalize removed - unused */

extern int irq_init_percpu_irqstack(unsigned int cpu);

struct irq_desc;
/* fixup_irqs, x86_platform_ipi_callback removed - no implementation */
extern void native_init_IRQ(void);

extern void __handle_irq(struct irq_desc *desc, struct pt_regs *regs);

extern void init_ISA_irqs(void);

extern void __init init_IRQ(void);


#endif  
