
#include <asm/apic.h>
#include <asm/hw_irq.h>
#include <asm/traps.h>

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

/* Merged from lib/irq_regs.c */
#include <asm/irq_regs.h>
DEFINE_PER_CPU(struct pt_regs *, __irq_regs);

void ack_bad_irq(unsigned int irq)
{
}

DEFINE_IDTENTRY_IRQ(common_interrupt)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	struct irq_desc *desc;

	desc = __this_cpu_read(vector_irq[vector]);
	if (likely(!IS_ERR_OR_NULL(desc))) {
		__handle_irq(desc, regs);
	} else {
		if (desc == VECTOR_UNUSED) {
			pr_emerg_ratelimited(
				"%s: %d.%u No irq handler for vector\n",
				__func__, smp_processor_id(), vector);
		} else {
			__this_cpu_write(vector_irq[vector], VECTOR_UNUSED);
		}
	}

	set_irq_regs(old_regs);
}
