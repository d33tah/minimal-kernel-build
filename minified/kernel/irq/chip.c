/* --- 2025-12-22 04:20 --- Stripped MSI infrastructure - unused */
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/cpumask.h>
#include <asm/hw_irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/irqdomain.h>

#include "internals.h"

static irqreturn_t bad_chained_irq(int irq, void *dev_id)
{
	WARN_ONCE(1, "Chained irq %d should not call an action\n", irq);
	return IRQ_NONE;
}

struct irqaction chained_action = {
	.handler = bad_chained_irq,
};

int irq_set_chip(unsigned int irq, const struct irq_chip *chip)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (!desc)
		return -EINVAL;

	desc->irq_data.chip = (struct irq_chip *)(chip ?: &no_irq_chip);
	irq_put_desc_unlock(desc, flags);
	/* irq_mark_irq removed - empty stub */
	return 0;
}

/* irq_set_irq_type, irq_set_chip_data, irq_get_irq_data removed - never called */

static void irq_state_clr_disabled(struct irq_desc *desc)
{
	irqd_clear(&desc->irq_data, IRQD_IRQ_DISABLED);
}

static void irq_state_clr_masked(struct irq_desc *desc)
{
	irqd_clear(&desc->irq_data, IRQD_IRQ_MASKED);
}

static int __irq_startup(struct irq_desc *desc)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);
	int ret = 0;

	WARN_ON_ONCE(!irqd_is_activated(d));

	if (d->chip->irq_startup) {
		ret = d->chip->irq_startup(d);
		irq_state_clr_disabled(desc);
		irq_state_clr_masked(desc);
	} else {
		irq_enable(desc);
	}
	irqd_set(&desc->irq_data, IRQD_IRQ_STARTED);
	return ret;
}

int irq_startup(struct irq_desc *desc, bool resend, bool force)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);
	int ret = 0;

	desc->depth = 0;

	if (irqd_is_started(d)) {
		irq_enable(desc);
	} else {
		/* __irq_startup_managed always returns IRQ_STARTUP_NORMAL */
		ret = __irq_startup(desc);
	}
	if (resend)
		check_irq_resend(desc, false);

	return ret;
}

int irq_activate(struct irq_desc *desc)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);

	if (!irqd_affinity_is_managed(d))
		return irq_domain_activate_irq(d, false);
	return 0;
}

int irq_activate_and_startup(struct irq_desc *desc, bool resend)
{
	if (WARN_ON(irq_activate(desc)))
		return 0;
	return irq_startup(desc, resend, IRQ_START_FORCE);
}

static void __irq_disable(struct irq_desc *desc, bool mask);

void irq_shutdown(struct irq_desc *desc)
{
	if (irqd_is_started(&desc->irq_data)) {
		desc->depth = 1;
		if (desc->irq_data.chip->irq_shutdown) {
			desc->irq_data.chip->irq_shutdown(&desc->irq_data);
			irq_state_set_disabled(desc);
			irq_state_set_masked(desc);
		} else {
			__irq_disable(desc, true);
		}
		irqd_clear(&desc->irq_data, IRQD_IRQ_STARTED);
	}
}

/* irq_shutdown_and_deactivate removed - never called */

void irq_enable(struct irq_desc *desc)
{
	if (!irqd_irq_disabled(&desc->irq_data)) {
		unmask_irq(desc);
	} else {
		irq_state_clr_disabled(desc);
		if (desc->irq_data.chip->irq_enable) {
			desc->irq_data.chip->irq_enable(&desc->irq_data);
			irq_state_clr_masked(desc);
		} else {
			unmask_irq(desc);
		}
	}
}

static void __irq_disable(struct irq_desc *desc, bool mask)
{
	if (irqd_irq_disabled(&desc->irq_data)) {
		if (mask)
			mask_irq(desc);
	} else {
		irq_state_set_disabled(desc);
		if (desc->irq_data.chip->irq_disable) {
			desc->irq_data.chip->irq_disable(&desc->irq_data);
			irq_state_set_masked(desc);
		} else if (mask) {
			mask_irq(desc);
		}
	}
}

void irq_disable(struct irq_desc *desc)
{
	__irq_disable(desc, irq_settings_disable_unlazy(desc));
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
		irq_state_clr_masked(desc);
	}
}

void unmask_threaded_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = desc->irq_data.chip;

	if (chip->flags & IRQCHIP_EOI_THREADED)
		chip->irq_eoi(&desc->irq_data);

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

static void __irq_do_set_handler(struct irq_desc *desc,
				 irq_flow_handler_t handle, int is_chained,
				 const char *name)
{
	if (!handle) {
		handle = handle_bad_irq;
	} else {
		struct irq_data *irq_data = &desc->irq_data;
		if (WARN_ON(!irq_data || irq_data->chip == &no_irq_chip))
			return;
	}

	if (handle == handle_bad_irq) {
		if (desc->irq_data.chip != &no_irq_chip)
			mask_ack_irq(desc);
		irq_state_set_disabled(desc);
		if (is_chained) {
			desc->action = NULL;
			WARN_ON(irq_chip_pm_put(irq_desc_get_irq_data(desc)));
		}
		desc->depth = 1;
	}
	desc->handle_irq = handle;
	desc->name = name;

	if (handle != handle_bad_irq && is_chained) {
		unsigned int type = irqd_get_trigger_type(&desc->irq_data);

		if (type != IRQ_TYPE_NONE) {
			__irq_set_trigger(desc, type);
			desc->handle_irq = handle;
		}

		irq_settings_set_noprobe(desc);
		irq_settings_set_norequest(desc);
		irq_settings_set_nothread(desc);
		desc->action = &chained_action;
		WARN_ON(irq_chip_pm_get(irq_desc_get_irq_data(desc)));
		irq_activate_and_startup(desc, IRQ_RESEND);
	}
}

void __irq_set_handler(unsigned int irq, irq_flow_handler_t handle,
		       int is_chained, const char *name)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, 0);

	if (!desc)
		return;

	__irq_do_set_handler(desc, handle, is_chained, name);
	irq_put_desc_busunlock(desc, flags);
}

void irq_set_chip_and_handler_name(unsigned int irq,
				   const struct irq_chip *chip,
				   irq_flow_handler_t handle, const char *name)
{
	irq_set_chip(irq, chip);
	__irq_set_handler(irq, handle, 0, name);
}

void irq_modify_status(unsigned int irq, unsigned long clr, unsigned long set)
{
	unsigned long flags, trigger, tmp;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (!desc)
		return;

	WARN_ON_ONCE(!desc->depth && (set & _IRQ_NOAUTOEN));

	irq_settings_clr_and_set(desc, clr, set);

	trigger = irqd_get_trigger_type(&desc->irq_data);

	irqd_clear(&desc->irq_data, IRQD_NO_BALANCING | IRQD_PER_CPU |
					    IRQD_TRIGGER_MASK | IRQD_LEVEL |
					    IRQD_MOVE_PCNTXT);
	if (irq_settings_has_no_balance_set(desc))
		irqd_set(&desc->irq_data, IRQD_NO_BALANCING);
	if (irq_settings_is_per_cpu(desc))
		irqd_set(&desc->irq_data, IRQD_PER_CPU);
	if (irq_settings_can_move_pcntxt(desc))
		irqd_set(&desc->irq_data, IRQD_MOVE_PCNTXT);
	if (irq_settings_is_level(desc))
		irqd_set(&desc->irq_data, IRQD_LEVEL);

	tmp = irq_settings_get_trigger_mask(desc);
	if (tmp != IRQ_TYPE_NONE)
		trigger = tmp;

	irqd_set(&desc->irq_data, trigger);

	irq_put_desc_unlock(desc, flags);
}

int irq_chip_pm_get(struct irq_data *data)
{
	/* CONFIG_PM not enabled */
	return 0;
}

int irq_chip_pm_put(struct irq_data *data)
{
	/* CONFIG_PM not enabled */
	return 0;
}
