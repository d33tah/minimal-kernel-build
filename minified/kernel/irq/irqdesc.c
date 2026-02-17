#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/radix-tree.h>
#include <linux/irqdomain.h>

#include "internals.h"

int nr_irqs = NR_IRQS;

static void irq_kobj_release(struct kobject *kobj);

static struct kobj_type irq_kobj_type = {
	.release = irq_kobj_release,
};

static RADIX_TREE(irq_desc_tree, GFP_KERNEL);

struct irq_desc *irq_to_desc(unsigned int irq)
{
	return radix_tree_lookup(&irq_desc_tree, irq);
}

static struct irq_desc *alloc_desc(int irq, int node, unsigned int flags,
				   struct module *owner)
{
	struct irq_desc *desc;

	desc = kzalloc_node(sizeof(*desc), GFP_KERNEL, node);
	if (!desc)
		return NULL;

	raw_spin_lock_init(&desc->lock);
	mutex_init(&desc->request_mutex);
	init_waitqueue_head(&desc->wait_for_threads);

	desc->irq_data.common = &desc->irq_common_data;
	desc->irq_data.irq = irq;
	desc->irq_data.chip = &no_irq_chip;
	irq_settings_clr_and_set(desc, ~0, _IRQ_DEFAULT_INIT_FLAGS);
	irqd_set(&desc->irq_data, IRQD_IRQ_DISABLED);
	irqd_set(&desc->irq_data, IRQD_IRQ_MASKED);
	desc->handle_irq = handle_bad_irq;
	desc->depth = 1;
	desc->name = NULL;
	desc->owner = owner;
	irqd_set(&desc->irq_data, flags);
	kobject_init(&desc->kobj, &irq_kobj_type);

	return desc;
}

static void irq_kobj_release(struct kobject *kobj)
{
	struct irq_desc *desc = container_of(kobj, struct irq_desc, kobj);
	kfree(desc);
}

int __init early_irq_init(void)
{
	int i;
	int initcnt = NR_IRQS_LEGACY; /* arch_probe_nr_irqs inlined */
	struct irq_desc *desc;

	printk(KERN_INFO "NR_IRQS: %d, nr_irqs: %d, preallocated irqs: %d\n",
	       NR_IRQS, nr_irqs, initcnt);

	if (WARN_ON(nr_irqs > IRQ_BITMAP_BITS))
		nr_irqs = IRQ_BITMAP_BITS;

	if (WARN_ON(initcnt > IRQ_BITMAP_BITS))
		initcnt = IRQ_BITMAP_BITS;

	if (initcnt > nr_irqs)
		nr_irqs = initcnt;

	for (i = 0; i < initcnt; i++) {
		desc = alloc_desc(i, 0, 0,
				  NULL); /* node=0 (first_online_node) */
		radix_tree_insert(&irq_desc_tree, i, desc);
	}
	return 0; /* arch_early_irq_init inlined */
}

struct irq_desc *__irq_get_desc_lock(unsigned int irq, unsigned long *flags,
				     bool bus, unsigned int check)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (desc) {
		if (check & _IRQ_DESC_CHECK) {
			if ((check & _IRQ_DESC_PERCPU) &&
			    !irq_settings_is_per_cpu_devid(desc))
				return NULL;

			if (!(check & _IRQ_DESC_PERCPU) &&
			    irq_settings_is_per_cpu_devid(desc))
				return NULL;
		}

		if (bus)
			chip_bus_lock(desc);
		raw_spin_lock_irqsave(&desc->lock, *flags);
	}
	return desc;
}

void __irq_put_desc_unlock(struct irq_desc *desc, unsigned long flags, bool bus)
	__releases(&desc->lock)
{
	raw_spin_unlock_irqrestore(&desc->lock, flags);
	if (bus)
		chip_bus_sync_unlock(desc);
}
