#include <linux/interrupt.h>

#include "internals.h"

int irq_startup(struct irq_desc *desc, bool resend, bool force)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);
	int ret = 0;

	desc->depth = 0;

	if (irqd_is_started(d)) {
		irq_enable(desc);
	} else {
		WARN_ON_ONCE(!irqd_is_activated(d));
		if (d->chip->irq_startup) {
			ret = d->chip->irq_startup(d);
			irqd_clear(&desc->irq_data, IRQD_IRQ_DISABLED);
			irqd_clear(&desc->irq_data, IRQD_IRQ_MASKED);
		} else {
			irq_enable(desc);
		}
		irqd_set(&desc->irq_data, IRQD_IRQ_STARTED);
	}
	if (resend)
		check_irq_resend(desc, false);

	return ret;
}

int irq_activate(struct irq_desc *desc)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);

	return irq_domain_activate_irq(d, false);
}

void irq_enable(struct irq_desc *desc)
{
	irqd_clear(&desc->irq_data, IRQD_IRQ_DISABLED);
	unmask_irq(desc);
}

static inline void mask_ack_irq(struct irq_desc *desc)
{
	if (desc->irq_data.chip->irq_mask_ack) {
		desc->irq_data.chip->irq_mask_ack(&desc->irq_data);
		irq_state_set_masked(desc);
	} else {
		mask_irq(desc);
		if (desc->irq_data.chip->irq_ack)
			desc->irq_data.chip->irq_ack(&desc->irq_data);
	}
}

void mask_irq(struct irq_desc *desc)
{
	if (irqd_irq_masked(&desc->irq_data))
		return;

	if (desc->irq_data.chip->irq_mask) {
		desc->irq_data.chip->irq_mask(&desc->irq_data);
		irq_state_set_masked(desc);
	}
}

void unmask_irq(struct irq_desc *desc)
{
	if (!irqd_irq_masked(&desc->irq_data))
		return;

	if (desc->irq_data.chip->irq_unmask) {
		desc->irq_data.chip->irq_unmask(&desc->irq_data);
		irqd_clear(&desc->irq_data, IRQD_IRQ_MASKED);
	}
}

void handle_level_irq(struct irq_desc *desc)
{
	raw_spin_lock(&desc->lock);
	mask_ack_irq(desc);

	if (irqd_has_set(&desc->irq_data, IRQD_IRQ_INPROGRESS))
		goto out_unlock;

	if (unlikely(!desc->action || irqd_irq_disabled(&desc->irq_data)))
		goto out_unlock;

	kstat_incr_irqs_this_cpu(desc);
	handle_irq_event(desc);

	if (!irqd_irq_disabled(&desc->irq_data) &&
	    irqd_irq_masked(&desc->irq_data))
		unmask_irq(desc);

out_unlock:
	raw_spin_unlock(&desc->lock);
}

void __irq_set_handler(unsigned int irq, irq_flow_handler_t handle,
		       const char *name)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, 0);

	if (!desc)
		return;

	if (!handle)
		handle = handle_bad_irq;

	desc->handle_irq = handle;
	desc->name = name;

	irq_put_desc_busunlock(desc, flags);
}

void irq_set_chip_and_handler_name(unsigned int irq,
				   const struct irq_chip *chip,
				   irq_flow_handler_t handle, const char *name)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (desc) {
		desc->irq_data.chip = (struct irq_chip *)(chip ?: &no_irq_chip);
		irq_put_desc_unlock(desc, flags);
	}
	__irq_set_handler(irq, handle, name);
}
