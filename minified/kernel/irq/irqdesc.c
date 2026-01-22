#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/radix-tree.h>
#include <linux/bitmap.h>
#include <linux/irqdomain.h>
#include <linux/sysfs.h>

#include "internals.h"

/* desc_set_defaults inlined into alloc_desc */

int nr_irqs = NR_IRQS;

static DEFINE_MUTEX(sparse_irq_lock);
static DECLARE_BITMAP(allocated_irqs, IRQ_BITMAP_BITS);

static void irq_kobj_release(struct kobject *kobj);

static struct kobj_type irq_kobj_type = {
	.release = irq_kobj_release,
};

static RADIX_TREE(irq_desc_tree, GFP_KERNEL);
/* irq_insert_desc removed - inlined into 2 callers (~4 LOC) */

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

	desc->kstat_irqs = alloc_percpu(unsigned int);
	if (!desc->kstat_irqs)
		goto err_desc;

	raw_spin_lock_init(&desc->lock);
	/* lockdep_set_class removed - empty stub */
	mutex_init(&desc->request_mutex);
	init_waitqueue_head(&desc->wait_for_threads);

	/* desc_set_defaults inlined */
	desc->irq_common_data.handler_data = NULL;
	desc->irq_common_data.msi_desc = NULL;
	desc->irq_data.common = &desc->irq_common_data;
	desc->irq_data.irq = irq;
	desc->irq_data.chip = &no_irq_chip;
	desc->irq_data.chip_data = NULL;
	irq_settings_clr_and_set(desc, ~0, _IRQ_DEFAULT_INIT_FLAGS);
	irqd_set(&desc->irq_data, IRQD_IRQ_DISABLED);
	irqd_set(&desc->irq_data, IRQD_IRQ_MASKED);
	desc->handle_irq = handle_bad_irq;
	desc->depth = 1;
	desc->irq_count = 0;
	desc->irqs_unhandled = 0;
	desc->name = NULL;
	desc->owner = owner;
	*per_cpu_ptr(desc->kstat_irqs, 0) = 0;
	irqd_set(&desc->irq_data, flags);
	kobject_init(&desc->kobj, &irq_kobj_type);

	return desc;

err_desc:
	kfree(desc);
	return NULL;
}

static void irq_kobj_release(struct kobject *kobj)
{
	struct irq_desc *desc = container_of(kobj, struct irq_desc, kobj);

	free_percpu(desc->kstat_irqs);
	kfree(desc);
}

/* delayed_free_desc, free_desc, alloc_descs removed -
   only caller was __irq_alloc_descs which was never called */

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
		set_bit(i, allocated_irqs);
		radix_tree_insert(&irq_desc_tree, i, desc);
	}
	return 0; /* arch_early_irq_init inlined */
}

int handle_irq_desc(struct irq_desc *desc)
{
	if (!desc)
		return -EINVAL;

	/* handle_enforce_irqctx always returns false - check removed */
	generic_handle_irq_desc(desc);
	return 0;
}

int generic_handle_irq(unsigned int irq)
{
	return handle_irq_desc(irq_to_desc(irq));
}

int generic_handle_irq_safe(unsigned int irq)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret = handle_irq_desc(irq_to_desc(irq));
	local_irq_restore(flags);
	return ret;
}

/* irq_free_descs, __irq_alloc_descs removed - never called */

unsigned int irq_get_next_irq(unsigned int offset)
{
	return find_next_bit(allocated_irqs, nr_irqs, offset);
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
