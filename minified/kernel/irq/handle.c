
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

#include <asm/irq_regs.h>

#include "internals.h"

void handle_bad_irq(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);

	print_irq_desc(irq, desc);
	kstat_incr_irqs_this_cpu(desc);
	ack_bad_irq(irq);
}

irqreturn_t no_action(int cpl, void *dev_id)
{
	return IRQ_NONE;
}

/* __irq_wake_thread removed - no threaded IRQs in minimal kernel */

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

	/* Removed: add_interrupt_randomness - not needed for minimal kernel */
	/* note_interrupt removed - was empty stub */
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
