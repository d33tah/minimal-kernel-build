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


struct software_node;


/* Minimal property_entry - only needs to exist as a type, never instantiated */
struct property_entry;

/* property_entries_free, is_software_node, to_software_node, software_node_fwnode,
   software_node_find_by_name, software_node_register_nodes, software_node_unregister_nodes,
   software_node_register_node_group, software_node_unregister_node_group,
   software_node_register, software_node_unregister, fwnode_remove_software_node,
   device_add_software_node, device_remove_software_node, device_create_managed_software_node
   removed - unused */

struct software_node {
	const char *name;
	const struct software_node *parent;
	const struct property_entry *properties;
};

#endif /* _LINUX_PROPERTY_H_ */
