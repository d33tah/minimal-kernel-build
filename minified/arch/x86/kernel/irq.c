#include <linux/smp.h>
#include <linux/irq.h>

#include <asm/apic.h>
#include <asm/traps.h>


DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

void ack_bad_irq(unsigned int irq)
{
	ack_APIC_irq();
}


static __always_inline void handle_irq(struct irq_desc *desc, struct pt_regs *regs)
{
	__handle_irq(desc, regs);
}

DEFINE_IDTENTRY_IRQ(common_interrupt)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	struct irq_desc *desc;

	 
	RCU_LOCKDEP_WARN(!rcu_is_watching(), "IRQ failed to wake up RCU");

	desc = __this_cpu_read(vector_irq[vector]);
	if (likely(!IS_ERR_OR_NULL(desc))) {
		handle_irq(desc, regs);
	} else {
		ack_APIC_irq();

		if (desc == VECTOR_UNUSED) {
			pr_emerg_ratelimited("%s: %d.%u No irq handler for vector\n", __func__, smp_processor_id(), vector);
		} else {
			__this_cpu_write(vector_irq[vector], VECTOR_UNUSED);
		}
	}

	set_irq_regs(old_regs);
}





