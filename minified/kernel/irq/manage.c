
#define pr_fmt(fmt) "genirq: " fmt

#include <linux/irq.h>

#include "internals.h"


/*
 * Runtime-dead (HIT=False): the only call sites (__setup_irq when
 * IRQF_TRIGGER_MASK is set, and __irq_do_set_handler for a chained irq with a
 * non-NONE type) never fire on a boot-once-and-print artifact. Safe-fallback
 * stub: return 0 -- the same value the live early-exit (!chip->irq_set_type)
 * path returned, i.e. "trigger configured OK, nothing to do". Both callers
 * tolerate ret==0. The full set_type / mask-unmask body is dead.
 */
int __irq_set_trigger(struct irq_desc *desc, unsigned long flags) {
	return 0; }

static int irq_request_resources(struct irq_desc *desc) {
	struct irq_data *d = &desc->irq_data;
	struct irq_chip *c = d->chip;

	return c->irq_request_resources ? c->irq_request_resources(d) : 0; }

static void irq_release_resources(struct irq_desc *desc) {
	struct irq_data *d = &desc->irq_data;
	struct irq_chip *c = d->chip;

	if (c->irq_release_resources)
		c->irq_release_resources(d); }

/*
 * The only two IRQs requested on this build are the legacy timer (irq 0,
 * IRQF_NOBALANCING|IRQF_IRQPOLL|IRQF_TIMER) and the 8259 cascade (irq 2,
 * IRQF_NO_THREAD), each registered EXACTLY ONCE. So desc->action is always
 * NULL on entry (shared == 0 unconditionally), and none of IRQF_SHARED /
 * IRQF_ONESHOT / IRQF_PERCPU / IRQF_TRIGGER_MASK is ever set. The shared-IRQ
 * handler-list walk, the oneshot thread_mask setup, the trigger-type config,
 * the per-cpu routing and the spurious-disable recovery are all statically
 * dead and have been folded out. Only the single-handler fresh-register path
 * survives.
 */
static int
__setup_irq(unsigned int irq, struct irq_desc *desc, struct irqaction *new) {
	unsigned long flags;
	int ret;

	if (!desc)
		return -EINVAL;

	if (desc->irq_data.chip == &no_irq_chip)
		return -ENOSYS;

	new->irq = irq;

	new->flags |= irqd_get_trigger_type(&desc->irq_data);

	mutex_lock(&desc->request_mutex);

	chip_bus_lock(desc);

	ret = irq_request_resources(desc);
	if (ret) {
		pr_err("Failed to request resources for %s (irq %d) on irqchip %s\n", new->name, irq, desc->irq_data.chip->name);
		goto out_bus_unlock; }

	raw_spin_lock_irqsave(&desc->lock, flags);

	ret = irq_activate(desc);
	if (ret)
		goto out_unlock;

	desc->istate &= ~(IRQS_AUTODETECT | IRQS_SPURIOUS_DISABLED | \
			  IRQS_ONESHOT | IRQS_WAITING);
	irqd_clear(&desc->irq_data, IRQD_IRQ_INPROGRESS);

	if (new->flags & IRQF_NOBALANCING) {
		irq_settings_set_no_balancing(desc);
		irqd_set(&desc->irq_data, IRQD_NO_BALANCING); }

	if (!(new->flags & IRQF_NO_AUTOEN) && irq_settings_can_autoenable(desc)) {
		irq_startup(desc, IRQ_RESEND, IRQ_START_COND);
	} else {
		desc->depth = 1; }

	desc->action = new;

	desc->irq_count = 0;
	desc->irqs_unhandled = 0;

	raw_spin_unlock_irqrestore(&desc->lock, flags);
	chip_bus_sync_unlock(desc);
	mutex_unlock(&desc->request_mutex);

	return 0;

out_unlock:
	raw_spin_unlock_irqrestore(&desc->lock, flags);

	if (!desc->action)
		irq_release_resources(desc);
out_bus_unlock:
	chip_bus_sync_unlock(desc);
	mutex_unlock(&desc->request_mutex);

	return ret; }

int request_threaded_irq(unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn, unsigned long irqflags, const char *devname, void *dev_id) {
	struct irqaction *action;
	struct irq_desc *desc;
	int retval;

	if (irq == IRQ_NOTCONNECTED)
		return -ENOTCONN;

	/*
	 * The flag-consistency validation (IRQF_SHARED w/o dev_id,
	 * IRQF_SHARED|IRQF_NO_AUTOEN, IRQF_COND_SUSPEND mismatches) is
	 * statically dead: the only two callers (timer irq0, cascade irq2)
	 * pass compile-constant irqflags that set none of IRQF_SHARED /
	 * IRQF_COND_SUSPEND / IRQF_NO_AUTOEN, with dev_id == NULL.
	 */

	desc = irq_to_desc(irq);
	if (!desc)
		return -EINVAL;

	if (!irq_settings_can_request(desc) || WARN_ON(irq_settings_is_per_cpu_devid(desc)))
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

	if (retval) {
		kfree(action); }

	return retval; }



/* irq_get_irqchip_state, irq_set_irqchip_state, irq_has_action,
 * irq_check_status_bit removed - not called */
