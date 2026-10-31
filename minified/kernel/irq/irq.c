#include <linux/irq.h>

/*
 * Legacy-PIC interrupt descriptors: NR_IRQS static descriptors, the level
 * flow handler and one action per IRQ (no shared IRQs, no threaded handlers,
 * no affinity: one CPU). The IRQs requested in this kernel are 0 (PIT timer)
 * and 2 (8259 cascade, no_action). Interrupt gates keep IF clear while a
 * handler runs and there is one CPU, so a descriptor's handler cannot be
 * re-entered.
 */
static struct irq_desc irq_descs[NR_IRQS];

static struct irq_chip no_irq_chip = {
	.name = "none",
};

struct irq_chip dummy_irq_chip = {
	.name = "dummy",
};

struct irq_desc *irq_to_desc(unsigned int irq)
{
	return irq < NR_IRQS ? &irq_descs[irq] : NULL;
}

void __init early_irq_init(void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++) {
		irq_descs[i].irq_data.irq = i;
		irq_descs[i].irq_data.chip = &no_irq_chip;
		irq_descs[i].handle_irq = handle_bad_irq;
		raw_spin_lock_init(&irq_descs[i].lock);
	}
}

/*
 * Upstream prints the descriptor, counts the IRQ in kstat and calls
 * ack_bad_irq(), which prints and acks the local APIC. None of printk, kstat
 * or the APIC exist here, so the deletion leaves an empty body. It is the
 * handler of every line no driver requested; those lines stay masked in the
 * 8259, so only a spurious IRQ 7/15 can reach it.
 */
void handle_bad_irq(struct irq_desc *desc)
{
}

irqreturn_t no_action(int cpl, void *dev_id)
{
	return IRQ_NONE;
}

/* Mask and ack the line, run the action, unmask. Without an action the
 * line stays masked. */
void handle_level_irq(struct irq_desc *desc)
{
	struct irqaction *action;

	raw_spin_lock(&desc->lock);
	desc->irq_data.chip->irq_mask_ack(&desc->irq_data);

	action = desc->action;
	if (action) {
		action->handler(desc->irq_data.irq, action->dev_id);
		desc->irq_data.chip->irq_unmask(&desc->irq_data);
	}
	raw_spin_unlock(&desc->lock);
}

void irq_set_chip_and_handler(unsigned int irq, struct irq_chip *chip,
			      irq_flow_handler_t handle)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long flags;

	if (!desc)
		return;

	raw_spin_lock_irqsave(&desc->lock, flags);
	desc->irq_data.chip = chip;
	desc->handle_irq = handle;
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

int request_irq(unsigned int irq, irq_handler_t handler, void *dev_id)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irqaction *action;
	unsigned long flags;

	if (!desc || !handler)
		return -EINVAL;
	if (desc->irq_data.chip == &no_irq_chip)
		return -ENOSYS;

	action = kzalloc(sizeof(*action), GFP_KERNEL);
	if (!action)
		return -ENOMEM;
	action->handler = handler;
	action->dev_id = dev_id;

	raw_spin_lock_irqsave(&desc->lock, flags);
	if (desc->action) {
		raw_spin_unlock_irqrestore(&desc->lock, flags);
		kfree(action);
		return -EBUSY;
	}
	desc->action = action;
	desc->irq_data.chip->irq_unmask(&desc->irq_data);
	raw_spin_unlock_irqrestore(&desc->lock, flags);

	return 0;
}
