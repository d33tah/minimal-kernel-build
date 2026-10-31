
#ifndef _ASM_X86_HW_IRQ_H
#define _ASM_X86_HW_IRQ_H

#include <asm/irq_vectors.h>

#ifndef __ASSEMBLY__

#include <linux/preempt.h>
#include <linux/smp.h>

extern char irq_entries_start[];

#define VECTOR_UNUSED NULL

typedef struct irq_desc *vector_irq_t[NR_VECTORS];
DECLARE_PER_CPU(vector_irq_t, vector_irq);

#endif

#endif
