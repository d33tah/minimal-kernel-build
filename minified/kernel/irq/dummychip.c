#include <linux/irq.h>

#include "internals.h"

static void ack_bad(struct irq_data *data) {
	/* Anchor-stub: no_irq_chip .irq_ack. A "bad" (unconnected) IRQ is
	 * never acked on this boot-once artifact (HIT=False), so the
	 * diagnostic print + ack_bad_irq are dead. Symbol kept for the
	 * no_irq_chip.irq_ack fn-ptr. */
}

static void noop(struct irq_data *data) { }

static unsigned int noop_ret(struct irq_data *data) {
	return 0;
}

struct irq_chip no_irq_chip = { .name		= "none", .irq_startup	= noop_ret, .irq_enable	= noop, .irq_disable	= noop, .irq_ack	= ack_bad, .flags		= IRQCHIP_SKIP_SET_WAKE, };

struct irq_chip dummy_irq_chip = { .name		= "dummy", .irq_startup	= noop_ret, .irq_enable	= noop, .irq_disable	= noop, .irq_ack	= noop, .irq_mask	= noop, .irq_unmask	= noop, .flags		= IRQCHIP_SKIP_SET_WAKE, };
