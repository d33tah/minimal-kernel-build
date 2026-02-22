#define pr_fmt(fmt) "genirq: " fmt

#include <linux/interrupt.h>
#include <linux/slab.h>

#include "internals.h"

static int __setup_irq(unsigned int irq, struct irq_desc *desc,
		       struct irqaction *new)
{
	unsigned long flags;

	if (!desc)
		return -EINVAL;
	if (desc->irq_data.chip == &no_irq_chip)
		return -ENOSYS;

	new->irq = irq;
	if (!(new->flags &IRQF_TRIGGER_MASK))
		new->flags |= irqd_get_trigger_type(&desc->irq_data);

	mutex_lock(&desc->request_mutex);
	chip_bus_lock(desc);
	raw_spin_lock_irqsave(&desc->lock, flags);

	irq_activate(desc);
	desc->istate &= ~(IRQS_AUTODETECT | IRQS_SPURIOUS_DISABLED |
			  IRQS_ONESHOT | IRQS_WAITING);
	irqd_clear(&desc->irq_data, IRQD_IRQ_INPROGRESS);

	if (!(new->flags &IRQF_NO_AUTOEN) &&
	    !(desc->status_use_accessors & _IRQ_NOAUTOEN))
		irq_startup(desc, IRQ_RESEND, IRQ_START_COND);
	else
		desc->depth = 1;

	desc->action = new;

	raw_spin_unlock_irqrestore(&desc->lock, flags);
	chip_bus_sync_unlock(desc);
	mutex_unlock(&desc->request_mutex);
	return 0;
}

int request_threaded_irq(unsigned int irq, irq_handler_t handler,
			 irq_handler_t thread_fn, unsigned long irqflags,
			 const char *devname, void *dev_id)
{
	struct irqaction *action;
	struct irq_desc *desc;
	int retval;

	desc = irq_to_desc(irq);
	if (!desc)
		return -EINVAL;

	if (!handler)
		return -EINVAL;

	action = kzalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->name = devname;
	action->dev_id = dev_id;

	retval = __setup_irq(irq, desc, action);
	if (retval)
		kfree(action);

	return retval;
}
