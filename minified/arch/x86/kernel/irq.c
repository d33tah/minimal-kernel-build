// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common interrupt code for 32 and 64 bit
 */
#include <linux/irq.h>

#include <asm/traps.h>

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

static __always_inline void handle_irq(struct irq_desc *desc,
				       struct pt_regs *regs)
{
	__handle_irq(desc, regs);
}

/*
 * common_interrupt() handles all normal device IRQ's (the special SMP
 * cross-CPU interrupts have their own entry points).
 */
DEFINE_IDTENTRY_IRQ(common_interrupt)
{
	struct irq_desc *desc;

	desc = __this_cpu_read(vector_irq[vector]);
	if (likely(!IS_ERR_OR_NULL(desc))) {
		handle_irq(desc, regs);
	} else {
		if (desc != VECTOR_UNUSED)
			__this_cpu_write(vector_irq[vector], VECTOR_UNUSED);
	}
}
