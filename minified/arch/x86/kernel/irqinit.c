#include <linux/irq.h>

#include <asm/timer.h>
#include <asm/setup.h>

/* inlined from asm/hw_irq.h */
extern char irq_entries_start[];

#define VECTOR_UNUSED NULL

typedef struct irq_desc *vector_irq_t[NR_VECTORS];
DECLARE_PER_CPU(vector_irq_t, vector_irq);
#include <asm/i8259.h>
#include <asm/traps.h>
#include <asm/irq_regs.h>

DEFINE_PER_CPU(vector_irq_t, vector_irq) = {
	[0 ... NR_VECTORS - 1] = VECTOR_UNUSED,
};

void __init init_ISA_irqs(void)
{
	struct irq_chip *chip = legacy_pic->chip;
	int i;

	legacy_pic->init(0);

	for (i = 0; i < nr_legacy_irqs(); i++)
		irq_set_chip_and_handler(i, chip, handle_level_irq);
}

void __init init_IRQ(void)
{
	int i;

	for (i = 0; i < nr_legacy_irqs(); i++)
		per_cpu(vector_irq, 0)[ISA_IRQ_VECTOR(i)] = irq_to_desc(i);

	BUG_ON(irq_init_percpu_irqstack(smp_processor_id()));

	x86_init.irqs.intr_init();
}

void __init native_init_IRQ(void)
{
	x86_init.irqs.pre_vector_init();

	idt_setup_apic_and_irq_gates();

	if (nr_legacy_irqs()) {
		if (request_irq(2, no_action, IRQF_NO_THREAD, "cascade", NULL))
			pr_err("%s: request_irq() failed\n", "cascade");
	}
}

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

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
