#define pr_fmt(fmt) "genirq: " fmt

#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/sched/task.h>
#include <linux/sched/isolation.h>
#include <linux/sched/types.h>
#include <linux/task_work.h>

#include "internals.h"

/* noirqdebug removed - always false, no way to set it */

/* force_irqthreads_key removed - macro never called */

/* __synchronize_hardirq removed - only caller was __free_irq which was removed */

/* __disable_irq, __disable_irq_nosync, disable_irq_nosync removed - never called */

void __enable_irq(struct irq_desc *desc)
{
	switch (desc->depth) {
	case 0:
err_out:
		WARN(1, KERN_WARNING "Unbalanced enable for IRQ %d\n",
		     irq_desc_get_irq(desc));
		break;
	case 1: {
		if (desc->istate & IRQS_SUSPENDED)
			goto err_out;

		irq_settings_set_noprobe(desc);

		irq_startup(desc, IRQ_RESEND, IRQ_START_FORCE);
		break;
	}
	default:
		desc->depth--;
	}
}

/* enable_irq removed - never called */

int __irq_set_trigger(struct irq_desc *desc, unsigned long flags)
{
	struct irq_chip *chip = desc->irq_data.chip;
	int ret, unmask = 0;

	if (!chip || !chip->irq_set_type) {
		return 0;
	}

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

static irqreturn_t irq_default_primary_handler(int irq, void *dev_id)
{
	return IRQ_WAKE_THREAD;
}

/* IRQ threading infrastructure removed - no threaded IRQs in minimal kernel */

static int __setup_irq(unsigned int irq, struct irq_desc *desc,
		       struct irqaction *new)
{
	struct irqaction *old, **old_ptr;
	unsigned long flags, thread_mask = 0;
	int ret, shared = 0;

	if (!desc)
		return -EINVAL;

	if (desc->irq_data.chip == &no_irq_chip)
		return -ENOSYS;

	new->irq = irq;

	if (!(new->flags &IRQF_TRIGGER_MASK))
		new->flags |= irqd_get_trigger_type(&desc->irq_data);

	if (desc->irq_data.chip->flags & IRQCHIP_ONESHOT_SAFE)
		new->flags &= ~IRQF_ONESHOT;

	mutex_lock(&desc->request_mutex);

	chip_bus_lock(desc);

	raw_spin_lock_irqsave(&desc->lock, flags);
	old_ptr = &desc->action;
	old = *old_ptr;
	if (old) {
		unsigned int oldtype;

		if (desc->istate & IRQS_NMI) {
			pr_err("Invalid attempt to share NMI for %s (irq %d) on irqchip %s.\n",
			       new->name, irq, desc->irq_data.chip->name);
			ret = -EINVAL;
			goto out_unlock;
		}

		if (irqd_trigger_type_was_set(&desc->irq_data)) {
			oldtype = irqd_get_trigger_type(&desc->irq_data);
		} else {
			oldtype = new->flags & IRQF_TRIGGER_MASK;
			irqd_set_trigger_type(&desc->irq_data, oldtype);
		}

		if (!((old->flags & new->flags) & IRQF_SHARED) ||
		    (oldtype != (new->flags &IRQF_TRIGGER_MASK)) ||
		    ((old->flags ^ new->flags) & IRQF_ONESHOT))
			goto mismatch;

		if ((old->flags & IRQF_PERCPU) != (new->flags &IRQF_PERCPU))
			goto mismatch;

		do {
			thread_mask |= old->thread_mask;
			old_ptr = &old->next;
			old = *old_ptr;
		} while (old);
		shared = 1;
	}

	if (new->flags & IRQF_ONESHOT) {
		if (thread_mask == ~0UL) {
			ret = -EBUSY;
			goto out_unlock;
		}

		new->thread_mask = 1UL << ffz(thread_mask);

	} else if (new->handler == irq_default_primary_handler &&
		   !(desc->irq_data.chip->flags & IRQCHIP_ONESHOT_SAFE)) {
		pr_err("Threaded irq requested with handler=NULL and !ONESHOT for %s (irq %d)\n",
		       new->name, irq);
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!shared) {
		if (new->flags & IRQF_TRIGGER_MASK) {
			ret = __irq_set_trigger(desc,
						new->flags &IRQF_TRIGGER_MASK);

			if (ret)
				goto out_unlock;
		}

		irq_activate(desc);

		desc->istate &= ~(IRQS_AUTODETECT | IRQS_SPURIOUS_DISABLED |
				  IRQS_ONESHOT | IRQS_WAITING);
		irqd_clear(&desc->irq_data, IRQD_IRQ_INPROGRESS);

		if (new->flags & IRQF_PERCPU) {
			irqd_set(&desc->irq_data, IRQD_PER_CPU);
			desc->status_use_accessors |= _IRQ_PER_CPU;
			if (new->flags & IRQF_NO_DEBUG)
				irq_settings_set_no_debug(desc);
		}

		if (new->flags & IRQF_ONESHOT)
			desc->istate |= IRQS_ONESHOT;

		if (new->flags & IRQF_NOBALANCING) {
			irq_settings_set_no_balancing(desc);
			irqd_set(&desc->irq_data, IRQD_NO_BALANCING);
		}

		if (!(new->flags &IRQF_NO_AUTOEN) &&
		    !(desc->status_use_accessors & _IRQ_NOAUTOEN)) {
			irq_startup(desc, IRQ_RESEND, IRQ_START_COND);
		} else {
			WARN_ON_ONCE(new->flags &IRQF_SHARED);

			desc->depth = 1;
		}

	} else if (new->flags & IRQF_TRIGGER_MASK) {
		unsigned int nmsk = new->flags & IRQF_TRIGGER_MASK;
		unsigned int omsk = irqd_get_trigger_type(&desc->irq_data);

		if (nmsk != omsk)

			pr_warn("irq %d uses trigger mode %u; requested %u\n",
				irq, omsk, nmsk);
	}

	*old_ptr = new;

	if (shared && (desc->istate & IRQS_SPURIOUS_DISABLED)) {
		desc->istate &= ~IRQS_SPURIOUS_DISABLED;
		__enable_irq(desc);
	}

	raw_spin_unlock_irqrestore(&desc->lock, flags);
	chip_bus_sync_unlock(desc);
	mutex_unlock(&desc->request_mutex);

	return 0;

mismatch:
	if (!(new->flags &IRQF_PROBE_SHARED)) {
		pr_err("Flags mismatch irq %d. %08x (%s) vs. %08x (%s)\n", irq,
		       new->flags, new->name, old->flags, old->name);
	}
	ret = -EBUSY;

out_unlock:
	raw_spin_unlock_irqrestore(&desc->lock, flags);

	chip_bus_sync_unlock(desc);
	mutex_unlock(&desc->request_mutex);

	module_put(desc->owner);
	return ret;
}

/* __free_irq/free_irq removed - never called */

int request_threaded_irq(unsigned int irq, irq_handler_t handler,
			 irq_handler_t thread_fn, unsigned long irqflags,
			 const char *devname, void *dev_id)
{
	struct irqaction *action;
	struct irq_desc *desc;
	int retval;

	if (irq == IRQ_NOTCONNECTED)
		return -ENOTCONN;

	if (((irqflags & IRQF_SHARED) && !dev_id) ||
	    ((irqflags & IRQF_SHARED) && (irqflags & IRQF_NO_AUTOEN)) ||
	    (!(irqflags & IRQF_SHARED) && (irqflags & IRQF_COND_SUSPEND)) ||
	    ((irqflags & IRQF_NO_SUSPEND) && (irqflags & IRQF_COND_SUSPEND)))
		return -EINVAL;

	desc = irq_to_desc(irq);
	if (!desc)
		return -EINVAL;

	if ((desc->status_use_accessors & _IRQ_NOREQUEST) ||
	    WARN_ON(irq_settings_is_per_cpu_devid(desc)))
		return -EINVAL;

	if (!handler) {
		if (!thread_fn)
			return -EINVAL;
		handler = irq_default_primary_handler;
	}

	action = kzalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->thread_fn = thread_fn;
	action->flags = irqflags;
	action->name = devname;
	action->dev_id = dev_id;

	retval = __setup_irq(irq, desc, action);

	if (retval) {
		kfree(action->secondary);
		kfree(action);
	}

	return retval;
}

/* __irq_get_irqchip_state, irq_get_irqchip_state, irq_set_irqchip_state,
 * irq_has_action, irq_check_status_bit removed - not called */
