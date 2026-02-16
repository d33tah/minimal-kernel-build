#define pr_fmt(fmt) "genirq: " fmt

#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/types.h>
#include <linux/task_work.h>

#include "internals.h"

/* Simplified __irq_set_trigger - only called for first install */
int __irq_set_trigger(struct irq_desc *desc, unsigned long flags)
{
	struct irq_chip *chip = desc->irq_data.chip;
	int ret, unmask = 0;

	if (!chip || !chip->irq_set_type)
		return 0;

	if (chip->flags & IRQCHIP_SET_TYPE_MASKED) {
		if (!irqd_irq_masked(&desc->irq_data))
			mask_irq(desc);
		if (!irqd_irq_disabled(&desc->irq_data))
			unmask = 1;
	}

	flags &= IRQ_TYPE_SENSE_MASK;
	ret = chip->irq_set_type(&desc->irq_data, flags);

	switch (ret) {
	case IRQ_SET_MASK_OK:
	case IRQ_SET_MASK_OK_DONE:
		irqd_clear(&desc->irq_data, IRQD_TRIGGER_MASK);
		irqd_set(&desc->irq_data, flags);
		fallthrough;
	case IRQ_SET_MASK_OK_NOCOPY:
		flags = irqd_get_trigger_type(&desc->irq_data);
		desc->status_use_accessors &= ~IRQ_TYPE_SENSE_MASK;
		desc->status_use_accessors |= flags & IRQ_TYPE_SENSE_MASK;
		irqd_clear(&desc->irq_data, IRQD_LEVEL);
		desc->status_use_accessors &= ~_IRQ_LEVEL;
		if (flags & IRQ_TYPE_LEVEL_MASK) {
			desc->status_use_accessors |= _IRQ_LEVEL;
			irqd_set(&desc->irq_data, IRQD_LEVEL);
		}
		ret = 0;
		break;
	default:
		pr_err("Setting trigger mode %lu for irq %u failed (%pS)\n",
		       flags, irq_desc_get_irq(desc), chip->irq_set_type);
	}
	if (unmask)
		unmask_irq(desc);
	return ret;
}

/* Simplified __setup_irq - no shared IRQ, no threading, no ONESHOT/PERCPU/NMI */
static int __setup_irq(unsigned int irq, struct irq_desc *desc,
		       struct irqaction *new)
{
	unsigned long flags;
	int ret;

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

	if (new->flags & IRQF_TRIGGER_MASK) {
		ret = __irq_set_trigger(desc, new->flags &IRQF_TRIGGER_MASK);
		if (ret)
			goto out_unlock;
	}

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

out_unlock:
	raw_spin_unlock_irqrestore(&desc->lock, flags);
	chip_bus_sync_unlock(desc);
	mutex_unlock(&desc->request_mutex);
	return ret;
}

/* Simplified request_threaded_irq - no thread_fn, no shared IRQ validation */
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
