
#ifndef _LINUX_IRQDOMAIN_H
#define _LINUX_IRQDOMAIN_H

#include <linux/types.h>
#include <linux/irqhandler.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/radix-tree.h>

struct device_node;
struct fwnode_handle;
struct irq_domain;
struct irq_chip;
struct irq_data;
struct irq_desc;
struct cpumask;
struct seq_file;
struct irq_affinity_desc;

#define IRQ_DOMAIN_IRQ_SPEC_PARAMS 16

struct irq_fwspec {
	struct fwnode_handle *fwnode;
	int param_count;
	u32 param[IRQ_DOMAIN_IRQ_SPEC_PARAMS];
};

void of_phandle_args_to_fwspec(struct device_node *np, const u32 *args,
			       unsigned int count, struct irq_fwspec *fwspec);

enum irq_domain_bus_token {
	DOMAIN_BUS_ANY		= 0,
};

struct irq_domain_ops {
	int (*match)(struct irq_domain *d, struct device_node *node,
		     enum irq_domain_bus_token bus_token);
	int (*select)(struct irq_domain *d, struct irq_fwspec *fwspec,
		      enum irq_domain_bus_token bus_token);
	int (*map)(struct irq_domain *d, unsigned int virq, irq_hw_number_t hw);
	void (*unmap)(struct irq_domain *d, unsigned int virq);
	int (*xlate)(struct irq_domain *d, struct device_node *node,
		     const u32 *intspec, unsigned int intsize,
		     unsigned long *out_hwirq, unsigned int *out_type);
};

extern const struct irq_domain_ops irq_generic_chip_ops;

struct irq_domain_chip_generic;

struct irq_domain {
	struct list_head link;
	const char *name;
	const struct irq_domain_ops *ops;
	void *host_data;
	unsigned int flags;
	unsigned int mapcount;

	 
	struct fwnode_handle *fwnode;
	enum irq_domain_bus_token bus_token;
	struct irq_domain_chip_generic *gc;
	struct device *dev;

	 
	irq_hw_number_t hwirq_max;
	unsigned int revmap_size;
	struct radix_tree_root revmap_tree;
	struct mutex revmap_mutex;
	struct irq_data __rcu *revmap[];
};

enum {
	 
	IRQ_DOMAIN_FLAG_HIERARCHY	= (1 << 0),

	 
	IRQ_DOMAIN_NAME_ALLOCATED	= (1 << 1),

	 
	IRQ_DOMAIN_FLAG_IPI_PER_CPU	= (1 << 2),

	 
	IRQ_DOMAIN_FLAG_IPI_SINGLE	= (1 << 3),

	 
	IRQ_DOMAIN_FLAG_MSI		= (1 << 4),

	 
	IRQ_DOMAIN_FLAG_MSI_REMAP	= (1 << 5),

	 
	IRQ_DOMAIN_MSI_NOMASK_QUIRK	= (1 << 6),

	 
	IRQ_DOMAIN_FLAG_NO_MAP		= (1 << 7),

	 
	IRQ_DOMAIN_FLAG_NONCORE		= (1 << 16),
};

static inline struct device_node *irq_domain_get_of_node(struct irq_domain *d)
{
	return to_of_node(d->fwnode);
}

static inline void irq_domain_set_pm_device(struct irq_domain *d,
					    struct device *dev)
{
	if (d)
		d->dev = dev;
}

static inline void irq_dispose_mapping(unsigned int virq) { }
static inline struct irq_domain *irq_find_matching_fwnode(
	struct fwnode_handle *fwnode, enum irq_domain_bus_token bus_token)
{
	return NULL;
}
static inline bool irq_domain_check_msi_remap(void)
{
	return false;
}

#endif  
