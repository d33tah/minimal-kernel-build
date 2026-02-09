#ifndef _LINUX_IRQDOMAIN_H
#define _LINUX_IRQDOMAIN_H
#include <linux/types.h>
struct irq_desc;
typedef void (*irq_flow_handler_t)(struct irq_desc *desc);
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/radix-tree.h>
struct device_node;
struct fwnode_handle;
struct irq_domain;
struct irq_data;
/* struct irq_affinity_desc removed - never used */
#define IRQ_DOMAIN_IRQ_SPEC_PARAMS 16
struct irq_fwspec { struct fwnode_handle *fwnode; int param_count; u32 param[IRQ_DOMAIN_IRQ_SPEC_PARAMS]; };
enum irq_domain_bus_token { DOMAIN_BUS_ANY = 0, };
struct irq_domain_ops {
	int (*match)(struct irq_domain *d, struct device_node *node, enum irq_domain_bus_token bus_token);
	int (*select)(struct irq_domain *d, struct irq_fwspec *fwspec, enum irq_domain_bus_token bus_token);
	int (*map)(struct irq_domain *d, unsigned int virq, irq_hw_number_t hw);
	void (*unmap)(struct irq_domain *d, unsigned int virq);
	int (*xlate)(struct irq_domain *d, struct device_node *node, const u32 *intspec, unsigned int intsize, unsigned long *out_hwirq, unsigned int *out_type);
};
/* struct irq_domain_chip_generic forward decl and gc field removed - never accessed */
struct irq_domain {
	struct list_head link; const char *name; const struct irq_domain_ops *ops;
	void *host_data; unsigned int flags; unsigned int mapcount;
	struct fwnode_handle *fwnode; enum irq_domain_bus_token bus_token;
	struct device *dev;
	irq_hw_number_t hwirq_max; unsigned int revmap_size;
	struct radix_tree_root revmap_tree; struct mutex revmap_mutex;
	struct irq_data __rcu *revmap[];
};
#endif
