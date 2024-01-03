// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006, Thomas Gleixner, Russell King
 *
 * This file contains the interrupt descriptor management code. Detailed
 * information is available in Documentation/core-api/genericirq.rst
 *
 */
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

/*
 * lockdep: we want to handle all irq_desc locks as a single lock-class:
 */
static struct lock_class_key irq_desc_lock_class;

static void __init init_irq_default_affinity(void)
{
}

static inline int
alloc_masks(struct irq_desc *desc, int node) { return 0; }
static inline void
desc_smp_init(struct irq_desc *desc, int node, const struct cpumask *affinity) { }

static void desc_set_defaults(unsigned int irq, struct irq_desc *desc, int node,
			      const struct cpumask *affinity, struct module *owner)
{
	int cpu;

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
	desc->tot_count = 0;
	desc->name = NULL;
	desc->owner = owner;
	for_each_possible_cpu(cpu)
		*per_cpu_ptr(desc->kstat_irqs, cpu) = 0;
	desc_smp_init(desc, node, affinity);
}

int nr_irqs = NR_IRQS;
EXPORT_SYMBOL_GPL(nr_irqs);

static DEFINE_MUTEX(sparse_irq_lock);
static DECLARE_BITMAP(allocated_irqs, IRQ_BITMAP_BITS);


static void irq_kobj_release(struct kobject *kobj);


static struct kobj_type irq_kobj_type = {
	.release	= irq_kobj_release,
};

static void irq_sysfs_add(int irq, struct irq_desc *desc) {}
static void irq_sysfs_del(struct irq_desc *desc) {}


static RADIX_TREE(irq_desc_tree, GFP_KERNEL);

static void irq_insert_desc(unsigned int irq, struct irq_desc *desc)
{
	radix_tree_insert(&irq_desc_tree, irq, desc);
}

struct irq_desc *irq_to_desc(unsigned int irq)
{
	return radix_tree_lookup(&irq_desc_tree, irq);
}

static void delete_irq_desc(unsigned int irq)
{
	radix_tree_delete(&irq_desc_tree, irq);
}

static inline void free_masks(struct irq_desc *desc) { }

void irq_lock_sparse(void)
{
	mutex_lock(&sparse_irq_lock);
}

void irq_unlock_sparse(void)
{
	mutex_unlock(&sparse_irq_lock);
}

static struct irq_desc *alloc_desc(int irq, int node, unsigned int flags,
				   const struct cpumask *affinity,
				   struct module *owner)
{
	struct irq_desc *desc;

	desc = kzalloc_node(sizeof(*desc), GFP_KERNEL, node);
	if (!desc)
		return NULL;
	/* allocate based on nr_cpu_ids */
	desc->kstat_irqs = alloc_percpu(unsigned int);
	if (!desc->kstat_irqs)
		goto err_desc;

	if (alloc_masks(desc, node))
		goto err_kstat;

	raw_spin_lock_init(&desc->lock);
	lockdep_set_class(&desc->lock, &irq_desc_lock_class);
	mutex_init(&desc->request_mutex);
	init_rcu_head(&desc->rcu);
	init_waitqueue_head(&desc->wait_for_threads);

	desc_set_defaults(irq, desc, node, affinity, owner);
	irqd_set(&desc->irq_data, flags);
	kobject_init(&desc->kobj, &irq_kobj_type);

	return desc;

err_kstat:
	free_percpu(desc->kstat_irqs);
err_desc:
	kfree(desc);
	return NULL;
}

static void irq_kobj_release(struct kobject *kobj)
{
	struct irq_desc *desc = container_of(kobj, struct irq_desc, kobj);

	free_masks(desc);
	free_percpu(desc->kstat_irqs);
	kfree(desc);
}

static void delayed_free_desc(struct rcu_head *rhp)
{
	struct irq_desc *desc = container_of(rhp, struct irq_desc, rcu);

	kobject_put(&desc->kobj);
}

static void free_desc(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	irq_remove_debugfs_entry(desc);
	unregister_irq_proc(irq, desc);

	/*
	 * sparse_irq_lock protects also show_interrupts() and
	 * kstat_irq_usr(). Once we deleted the descriptor from the
	 * sparse tree we can free it. Access in proc will fail to
	 * lookup the descriptor.
	 *
	 * The sysfs entry must be serialized against a concurrent
	 * irq_sysfs_init() as well.
	 */
	irq_sysfs_del(desc);
	delete_irq_desc(irq);

	/*
	 * We free the descriptor, masks and stat fields via RCU. That
	 * allows demultiplex interrupts to do rcu based management of
	 * the child interrupts.
	 * This also allows us to use rcu in kstat_irqs_usr().
	 */
	call_rcu(&desc->rcu, delayed_free_desc);
}

static int alloc_descs(unsigned int start, unsigned int cnt, int node,
		       const struct irq_affinity_desc *affinity,
		       struct module *owner)
{
	struct irq_desc *desc;
	int i;

	/* Validate affinity mask(s) */
	if (affinity) {
		for (i = 0; i < cnt; i++) {
			if (cpumask_empty(&affinity[i].mask))
				return -EINVAL;
		}
	}

	for (i = 0; i < cnt; i++) {
		const struct cpumask *mask = NULL;
		unsigned int flags = 0;

		if (affinity) {
			if (affinity->is_managed) {
				flags = IRQD_AFFINITY_MANAGED |
					IRQD_MANAGED_SHUTDOWN;
			}
			mask = &affinity->mask;
			node = cpu_to_node(cpumask_first(mask));
			affinity++;
		}

		desc = alloc_desc(start + i, node, flags, mask, owner);
		if (!desc)
			goto err;
		irq_insert_desc(start + i, desc);
		irq_sysfs_add(start + i, desc);
		irq_add_debugfs_entry(start + i, desc);
	}
	bitmap_set(allocated_irqs, start, cnt);
	return start;

err:
	for (i--; i >= 0; i--)
		free_desc(start + i);
	return -ENOMEM;
}

static int irq_expand_nr_irqs(unsigned int nr)
{
	if (nr > IRQ_BITMAP_BITS)
		return -ENOMEM;
	nr_irqs = nr;
	return 0;
}

int __init early_irq_init(void)
{
	int i, initcnt, node = first_online_node;
	struct irq_desc *desc;

	init_irq_default_affinity();

	/* Let arch update nr_irqs and return the nr of preallocated irqs */
	initcnt = arch_probe_nr_irqs();
	printk(KERN_INFO "NR_IRQS: %d, nr_irqs: %d, preallocated irqs: %d\n",
	       NR_IRQS, nr_irqs, initcnt);

	if (WARN_ON(nr_irqs > IRQ_BITMAP_BITS))
		nr_irqs = IRQ_BITMAP_BITS;

	if (WARN_ON(initcnt > IRQ_BITMAP_BITS))
		initcnt = IRQ_BITMAP_BITS;

	if (initcnt > nr_irqs)
		nr_irqs = initcnt;

	for (i = 0; i < initcnt; i++) {
		desc = alloc_desc(i, node, 0, NULL, NULL);
		set_bit(i, allocated_irqs);
		irq_insert_desc(i, desc);
	}
	return arch_early_irq_init();
}


int handle_irq_desc(struct irq_desc *desc)
{
	struct irq_data *data;

	if (!desc)
		return -EINVAL;

	data = irq_desc_get_irq_data(desc);
	if (WARN_ON_ONCE(!in_hardirq() && handle_enforce_irqctx(data)))
		return -EPERM;

	generic_handle_irq_desc(desc);
	return 0;
}

/**
 * generic_handle_irq - Invoke the handler for a particular irq
 * @irq:	The irq number to handle
 *
 * Returns:	0 on success, or -EINVAL if conversion has failed
 *
 * 		This function must be called from an IRQ context with irq regs
 * 		initialized.
  */
int generic_handle_irq(unsigned int irq)
{
	return handle_irq_desc(irq_to_desc(irq));
}
EXPORT_SYMBOL_GPL(generic_handle_irq);

/**
 * generic_handle_irq_safe - Invoke the handler for a particular irq from any
 *			     context.
 * @irq:	The irq number to handle
 *
 * Returns:	0 on success, a negative value on error.
 *
 * This function can be called from any context (IRQ or process context). It
 * will report an error if not invoked from IRQ context and the irq has been
 * marked to enforce IRQ-context only.
 */
int generic_handle_irq_safe(unsigned int irq)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret = handle_irq_desc(irq_to_desc(irq));
	local_irq_restore(flags);
	return ret;
}
EXPORT_SYMBOL_GPL(generic_handle_irq_safe);


/* Dynamic interrupt handling */

/**
 * irq_free_descs - free irq descriptors
 * @from:	Start of descriptor range
 * @cnt:	Number of consecutive irqs to free
 */
void irq_free_descs(unsigned int from, unsigned int cnt)
{
	int i;

	if (from >= nr_irqs || (from + cnt) > nr_irqs)
		return;

	mutex_lock(&sparse_irq_lock);
	for (i = 0; i < cnt; i++)
		free_desc(from + i);

	bitmap_clear(allocated_irqs, from, cnt);
	mutex_unlock(&sparse_irq_lock);
}
EXPORT_SYMBOL_GPL(irq_free_descs);

/**
 * __irq_alloc_descs - allocate and initialize a range of irq descriptors
 * @irq:	Allocate for specific irq number if irq >= 0
 * @from:	Start the search from this irq number
 * @cnt:	Number of consecutive irqs to allocate.
 * @node:	Preferred node on which the irq descriptor should be allocated
 * @owner:	Owning module (can be NULL)
 * @affinity:	Optional pointer to an affinity mask array of size @cnt which
 *		hints where the irq descriptors should be allocated and which
 *		default affinities to use
 *
 * Returns the first irq number or error code
 */
int __ref
__irq_alloc_descs(int irq, unsigned int from, unsigned int cnt, int node,
		  struct module *owner, const struct irq_affinity_desc *affinity)
{
	int start, ret;

	if (!cnt)
		return -EINVAL;

	if (irq >= 0) {
		if (from > irq)
			return -EINVAL;
		from = irq;
	} else {
		/*
		 * For interrupts which are freely allocated the
		 * architecture can force a lower bound to the @from
		 * argument. x86 uses this to exclude the GSI space.
		 */
		from = arch_dynirq_lower_bound(from);
	}

	mutex_lock(&sparse_irq_lock);

	start = bitmap_find_next_zero_area(allocated_irqs, IRQ_BITMAP_BITS,
					   from, cnt, 0);
	ret = -EEXIST;
	if (irq >=0 && start != irq)
		goto unlock;

	if (start + cnt > nr_irqs) {
		ret = irq_expand_nr_irqs(start + cnt);
		if (ret)
			goto unlock;
	}
	ret = alloc_descs(start, cnt, node, affinity, owner);
unlock:
	mutex_unlock(&sparse_irq_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(__irq_alloc_descs);

/**
 * irq_get_next_irq - get next allocated irq number
 * @offset:	where to start the search
 *
 * Returns next irq number after offset or nr_irqs if none is found.
 */
unsigned int irq_get_next_irq(unsigned int offset)
{
	return find_next_bit(allocated_irqs, nr_irqs, offset);
}

struct irq_desc *
__irq_get_desc_lock(unsigned int irq, unsigned long *flags, bool bus,
		    unsigned int check)
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

int irq_set_percpu_devid_partition(unsigned int irq,
				   const struct cpumask *affinity)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (!desc)
		return -EINVAL;

	if (desc->percpu_enabled)
		return -EINVAL;

	desc->percpu_enabled = kzalloc(sizeof(*desc->percpu_enabled), GFP_KERNEL);

	if (!desc->percpu_enabled)
		return -ENOMEM;

	if (affinity)
		desc->percpu_affinity = affinity;
	else
		desc->percpu_affinity = cpu_possible_mask;

	irq_set_percpu_devid_flags(irq);
	return 0;
}

int irq_set_percpu_devid(unsigned int irq)
{
	return irq_set_percpu_devid_partition(irq, NULL);
}

int irq_get_percpu_devid_partition(unsigned int irq, struct cpumask *affinity)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (!desc || !desc->percpu_enabled)
		return -EINVAL;

	if (affinity)
		cpumask_copy(affinity, desc->percpu_affinity);

	return 0;
}
EXPORT_SYMBOL_GPL(irq_get_percpu_devid_partition);

void kstat_incr_irq_this_cpu(unsigned int irq)
{
	kstat_incr_irqs_this_cpu(irq_to_desc(irq));
}

/**
 * kstat_irqs_cpu - Get the statistics for an interrupt on a cpu
 * @irq:	The interrupt number
 * @cpu:	The cpu number
 *
 * Returns the sum of interrupt counts on @cpu since boot for
 * @irq. The caller must ensure that the interrupt is not removed
 * concurrently.
 */
unsigned int kstat_irqs_cpu(unsigned int irq, int cpu)
{
	struct irq_desc *desc = irq_to_desc(irq);

	return desc && desc->kstat_irqs ?
			*per_cpu_ptr(desc->kstat_irqs, cpu) : 0;
}

static bool irq_is_nmi(struct irq_desc *desc)
{
	return desc->istate & IRQS_NMI;
}

static unsigned int kstat_irqs(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned int sum = 0;
	int cpu;

	if (!desc || !desc->kstat_irqs)
		return 0;
	if (!irq_settings_is_per_cpu_devid(desc) &&
	    !irq_settings_is_per_cpu(desc) &&
	    !irq_is_nmi(desc))
		return data_race(desc->tot_count);

	for_each_possible_cpu(cpu)
		sum += data_race(*per_cpu_ptr(desc->kstat_irqs, cpu));
	return sum;
}

/**
 * kstat_irqs_usr - Get the statistics for an interrupt from thread context
 * @irq:	The interrupt number
 *
 * Returns the sum of interrupt counts on all cpus since boot for @irq.
 *
 * It uses rcu to protect the access since a concurrent removal of an
 * interrupt descriptor is observing an rcu grace period before
 * delayed_free_desc()/irq_kobj_release().
 */
unsigned int kstat_irqs_usr(unsigned int irq)
{
	unsigned int sum;

	rcu_read_lock();
	sum = kstat_irqs(irq);
	rcu_read_unlock();
	return sum;
}

