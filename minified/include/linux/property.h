/* Minimal property.h - most functions stubbed/removed */
#ifndef _LINUX_PROPERTY_H_
#define _LINUX_PROPERTY_H_

#include <linux/bits.h>
#include <linux/fwnode.h>
#include <linux/types.h>

struct device;

enum dev_prop_type {
	DEV_PROP_U8,
	DEV_PROP_U16,
	DEV_PROP_U32,
	DEV_PROP_U64,
	DEV_PROP_STRING,
	DEV_PROP_REF,
};

enum dev_dma_attr {
	DEV_DMA_NOT_SUPPORTED,
	DEV_DMA_NON_COHERENT,
	DEV_DMA_COHERENT,
};

/* Most device_property_* and fwnode_property_* functions removed - unused */

struct software_node;

/* software_node_ref_args struct removed - never instantiated */

/* Minimal property_entry - only needs to exist as a type, never instantiated */
struct property_entry;

void property_entries_free(const struct property_entry *properties);

struct software_node {
	const char *name;
	const struct software_node *parent;
	const struct property_entry *properties;
};

bool is_software_node(const struct fwnode_handle *fwnode);
const struct software_node *to_software_node(const struct fwnode_handle *fwnode);
struct fwnode_handle *software_node_fwnode(const struct software_node *node);

const struct software_node *
software_node_find_by_name(const struct software_node *parent, const char *name);

int software_node_register_nodes(const struct software_node *nodes);
void software_node_unregister_nodes(const struct software_node *nodes);

int software_node_register_node_group(const struct software_node **node_group);
void software_node_unregister_node_group(const struct software_node **node_group);

int software_node_register(const struct software_node *node);
void software_node_unregister(const struct software_node *node);

void fwnode_remove_software_node(struct fwnode_handle *fwnode);

int device_add_software_node(struct device *dev, const struct software_node *node);
void device_remove_software_node(struct device *dev);

int device_create_managed_software_node(struct device *dev,
					const struct property_entry *properties,
					const struct software_node *parent);

#endif /* _LINUX_PROPERTY_H_ */
