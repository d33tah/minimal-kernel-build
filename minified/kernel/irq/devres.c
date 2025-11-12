// SPDX-License-Identifier: GPL-2.0
/*
 * Device resource management aware IRQ - Stubbed minimal implementation
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/errno.h>

int devm_request_threaded_irq(struct device *dev, unsigned int irq,
			       irq_handler_t handler, irq_handler_t thread_fn,
			       unsigned long irqflags, const char *devname,
			       void *dev_id)
{
	return -ENOSYS;
}
EXPORT_SYMBOL(devm_request_threaded_irq);

int devm_request_any_context_irq(struct device *dev, unsigned int irq,
				  irq_handler_t handler, unsigned long irqflags,
				  const char *devname, void *dev_id)
{
	return -ENOSYS;
}
EXPORT_SYMBOL(devm_request_any_context_irq);

void devm_free_irq(struct device *dev, unsigned int irq, void *dev_id) { }
EXPORT_SYMBOL(devm_free_irq);

int __devm_irq_alloc_descs(struct device *dev, int irq, unsigned int from,
			   unsigned int cnt, int node, struct module *owner,
			   const struct irq_affinity_desc *affinity)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(__devm_irq_alloc_descs);
