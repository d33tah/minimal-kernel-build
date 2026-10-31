#include <linux/irq.h>

#include <asm/desc.h>
#include <asm/i8259.h>

DEFINE_PER_CPU(vector_irq_t, vector_irq) = {
	[0 ... NR_VECTORS - 1] = VECTOR_UNUSED,
};

static void __init init_ISA_irqs(void)
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

	native_init_IRQ();
}

void __init native_init_IRQ(void)
{
	init_ISA_irqs();

	idt_setup_apic_and_irq_gates();

	if (request_irq(2, no_action, NULL))
		pr_err("%s: request_irq() failed\n", "cascade");
}
