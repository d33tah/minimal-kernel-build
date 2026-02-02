#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/of.h>
/* seq_file.h removed - header is empty */
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/irq.h>

#include <asm/irq_stack.h>
#include <asm/apic.h>
#include <asm/irq.h>
/* mce.h removed - header is empty */
#include <asm/hw_irq.h>
#include <asm/desc.h>
#include <asm/traps.h>

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

/* Merged from lib/irq_regs.c */
#include <asm/irq_regs.h>
DEFINE_PER_CPU(struct pt_regs *, __irq_regs);
/* irq_err_count removed - only incremented, never read */

void ack_bad_irq(unsigned int irq)
{
	/* printk_ratelimit() always returns 0, ack_APIC_irq is empty stub */
}

/* handle_irq merged - was just calling __handle_irq */
DEFINE_IDTENTRY_IRQ(common_interrupt)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	struct irq_desc *desc;

	desc = __this_cpu_read(vector_irq[vector]);
	if (likely(!IS_ERR_OR_NULL(desc))) {
		__handle_irq(desc, regs);
	} else {
		/* ack_APIC_irq removed - empty stub */
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
