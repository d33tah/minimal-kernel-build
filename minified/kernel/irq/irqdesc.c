#include <linux/irq.h>
#include <linux/radix-tree.h>

#include "internals.h"

static struct lock_class_key irq_desc_lock_class;

static void desc_set_defaults(unsigned int irq, struct irq_desc *desc) {
	int cpu;

	desc->irq_data.common = &desc->irq_common_data;
	desc->irq_data.irq = irq;
	desc->irq_data.chip = &no_irq_chip;
	irq_settings_clr_and_set(desc, ~0, _IRQ_DEFAULT_INIT_FLAGS);
	irqd_set(&desc->irq_data, IRQD_IRQ_DISABLED);
	irqd_set(&desc->irq_data, IRQD_IRQ_MASKED);
	desc->handle_irq = handle_bad_irq;
	desc->depth = 1;
	desc->irq_count = 0;
	desc->irqs_unhandled = 0;
	for_each_possible_cpu(cpu)
		*per_cpu_ptr(desc->kstat_irqs, cpu) = 0;
}

int nr_irqs = NR_IRQS;

static DECLARE_BITMAP(allocated_irqs, IRQ_BITMAP_BITS);


static struct kobj_type irq_kobj_type = { };

static RADIX_TREE(irq_desc_tree, GFP_KERNEL);

static void irq_insert_desc(unsigned int irq, struct irq_desc *desc) {
	radix_tree_insert(&irq_desc_tree, irq, desc);
}

struct irq_desc *irq_to_desc(unsigned int irq) {
	return radix_tree_lookup(&irq_desc_tree, irq);
}

static struct irq_desc *alloc_desc(int irq, int node, unsigned int flags) {
	struct irq_desc *desc;

	desc = kzalloc_node(sizeof(*desc), GFP_KERNEL, node);
	if (!desc)
		return NULL;
	 
	desc->kstat_irqs = alloc_percpu(unsigned int);
	if (!desc->kstat_irqs)
		goto err_desc;

	raw_spin_lock_init(&desc->lock);
	lockdep_set_class(&desc->lock, &irq_desc_lock_class);
	mutex_init(&desc->request_mutex);

	desc_set_defaults(irq, desc);
	irqd_set(&desc->irq_data, flags);
	kobject_init(&desc->kobj, &irq_kobj_type);

	return desc;

err_desc:
	kfree(desc);
	return NULL;
}

int __init early_irq_init(void) {
	int i, initcnt, node = first_online_node;
	struct irq_desc *desc;

	initcnt = arch_probe_nr_irqs();
	printk(KERN_INFO "NR_IRQS: %d, nr_irqs: %d, preallocated irqs: %d\n", NR_IRQS, nr_irqs, initcnt);

	if (WARN_ON(nr_irqs > IRQ_BITMAP_BITS))
		nr_irqs = IRQ_BITMAP_BITS;

	if (WARN_ON(initcnt > IRQ_BITMAP_BITS))
		initcnt = IRQ_BITMAP_BITS;

	if (initcnt > nr_irqs)
		nr_irqs = initcnt;

	for (i = 0; i < initcnt; i++) {
		desc = alloc_desc(i, node, 0);
		set_bit(i, allocated_irqs);
		irq_insert_desc(i, desc);
	}
	return arch_early_irq_init();
}


struct irq_desc * __irq_get_desc_lock(unsigned int irq, unsigned long *flags, bool bus, unsigned int check) {
	struct irq_desc *desc = irq_to_desc(irq);

	if (desc) {
		if (check & _IRQ_DESC_CHECK) {
			if ((check & _IRQ_DESC_PERCPU) && !irq_settings_is_per_cpu_devid(desc))
				return NULL;

			if (!(check & _IRQ_DESC_PERCPU) && irq_settings_is_per_cpu_devid(desc))
				return NULL;
		}

		if (bus)
			chip_bus_lock(desc);
		raw_spin_lock_irqsave(&desc->lock, *flags);
	}
	return desc;
}

void __irq_put_desc_unlock(struct irq_desc *desc, unsigned long flags, bool bus)
	__releases(&desc->lock) {
	raw_spin_unlock_irqrestore(&desc->lock, flags);
	if (bus)
		chip_bus_sync_unlock(desc);
}



