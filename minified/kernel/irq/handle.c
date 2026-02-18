
#include <linux/interrupt.h>

#include "internals.h"

void handle_bad_irq(struct irq_desc *desc)
{
}

irqreturn_t no_action(int cpl, void *dev_id)
{
	return IRQ_NONE;
}

irqreturn_t __handle_irq_event_percpu(struct irq_desc *desc)
{
	irqreturn_t retval = IRQ_NONE;
	unsigned int irq = desc->irq_data.irq;
	struct irqaction *action;

	for_each_action_of_desc(desc, action)
	{
		irqreturn_t res;

		res = action->handler(irq, action->dev_id);

		if (WARN_ONCE(!irqs_disabled(),
			      "irq %u handler %pS enabled interrupts\n", irq,
			      action->handler))
			local_irq_disable();

		switch (res) {
		default:
			break;
		}

		retval |= res;
	}

	return retval;
}

irqreturn_t handle_irq_event_percpu(struct irq_desc *desc)
{
	irqreturn_t retval;

	retval = __handle_irq_event_percpu(desc);

	return retval;
}

irqreturn_t handle_irq_event(struct irq_desc *desc)
{
	irqreturn_t ret;

	desc->istate &= ~IRQS_PENDING;
	irqd_set(&desc->irq_data, IRQD_IRQ_INPROGRESS);
	raw_spin_unlock(&desc->lock);

	ret = handle_irq_event_percpu(desc);

	raw_spin_lock(&desc->lock);
	irqd_clear(&desc->irq_data, IRQD_IRQ_INPROGRESS);
	return ret;
}
