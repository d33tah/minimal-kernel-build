/* --- 2025-12-22 04:20 --- Stripped MSI infrastructure - unused */
#include <linux/irq.h>
/* linux/module.h removed - no module usage */
#include <linux/cpumask.h>
#include <asm/hw_irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

#include "internals.h"

/* bad_chained_irq, chained_action removed - is_chained path never taken (~8 LOC) */

/* irq_set_chip removed - inlined into irq_set_chip_and_handler_name (~11 LOC) */
/* irq_set_irq_type, irq_set_chip_data, irq_get_irq_data removed - never called */
/* irq_state_clr_disabled, irq_state_clr_masked removed - inlined into callers (~8 LOC) */
/* __irq_startup removed - inlined into single caller (~16 LOC) */

int irq_startup(struct irq_desc *desc, bool resend, bool force)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);
	int ret = 0;

	desc->depth = 0;

	if (irqd_is_started(d)) {
		irq_enable(desc);
	} else {
		/* Inlined __irq_startup */
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

	/* irqd_affinity_is_managed always false - IRQD_AFFINITY_MANAGED never set */
	return irq_domain_activate_irq(d, false);
}

/* irq_activate_and_startup removed - only called from is_chained path (~5 LOC) */

/* __irq_disable, irq_disable removed - zero callers */

void irq_enable(struct irq_desc *desc)
{
	if (!irqd_irq_disabled(&desc->irq_data)) {
		unmask_irq(desc);
	} else {
		irqd_clear(&desc->irq_data, IRQD_IRQ_DISABLED);
		if (desc->irq_data.chip->irq_enable) {
			desc->irq_data.chip->irq_enable(&desc->irq_data);
			irqd_clear(&desc->irq_data, IRQD_IRQ_MASKED);
		} else {
			unmask_irq(desc);
		}
	}
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

void unmask_threaded_irq(struct irq_desc *desc)
{
	/* IRQCHIP_EOI_THREADED check removed - never set by any chip */
	unmask_irq(desc);
}

void handle_level_irq(struct irq_desc *desc)
{
	raw_spin_lock(&desc->lock);
	mask_ack_irq(desc);

	/* irq_may_run inlined */
	if (irqd_has_set(&desc->irq_data,
			 IRQD_IRQ_INPROGRESS | IRQD_WAKEUP_ARMED))
		goto out_unlock;

	desc->istate &= ~(IRQS_REPLAY | IRQS_WAITING);

	if (unlikely(!desc->action || irqd_irq_disabled(&desc->irq_data))) {
		desc->istate |= IRQS_PENDING;
		goto out_unlock;
	}

	kstat_incr_irqs_this_cpu(desc);
	handle_irq_event(desc);

	/* cond_unmask_irq inlined */
	if (!irqd_irq_disabled(&desc->irq_data) &&
	    irqd_irq_masked(&desc->irq_data) && !desc->threads_oneshot)
		unmask_irq(desc);

out_unlock:
	raw_spin_unlock(&desc->lock);
}

/* __irq_do_set_handler inlined into __irq_set_handler */

void __irq_set_handler(unsigned int irq, irq_flow_handler_t handle,
		       const char *name)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, 0);

	if (!desc)
		return;

	if (!handle) {
		handle = handle_bad_irq;
	} else {
		struct irq_data *irq_data = &desc->irq_data;
		if (WARN_ON(!irq_data || irq_data->chip == &no_irq_chip))
			goto out;
	}

	if (handle == handle_bad_irq) {
		if (desc->irq_data.chip != &no_irq_chip)
			mask_ack_irq(desc);
		irq_state_set_disabled(desc);
		desc->depth = 1;
	}
	desc->handle_irq = handle;
	desc->name = name;

	/* is_chained path removed - always called with is_chained=0 (~15 LOC) */

out:
	irq_put_desc_busunlock(desc, flags);
}

void irq_set_chip_and_handler_name(unsigned int irq,
				   const struct irq_chip *chip,
				   irq_flow_handler_t handle, const char *name)
{
	/* irq_set_chip inlined */
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (desc) {
		desc->irq_data.chip = (struct irq_chip *)(chip ?: &no_irq_chip);
		irq_put_desc_unlock(desc, flags);
	}
	__irq_set_handler(irq, handle, name);
}

/* irq_modify_status removed - never called */
/* irq_chip_pm_get/irq_chip_pm_put removed - always return 0, calls removed */
