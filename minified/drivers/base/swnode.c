// SPDX-License-Identifier: GPL-2.0
/*
 * swnode.c - Software nodes for the firmware node framework (STUBBED)
 *
 * Minimal stub - software node functionality not needed for minimal kernel
 * with basic TTY console support.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/property.h>
#include <linux/errno.h>

bool is_software_node(const struct fwnode_handle *fwnode)
{
	return false;
}

const struct software_node *to_software_node(const struct fwnode_handle *fwnode)
{
	return NULL;
}

void property_entries_free(const struct property_entry *properties)
{
}

const struct software_node *
software_node_find_by_name(const struct software_node *parent, const char *name)
{
	return NULL;
}

int software_node_register_nodes(const struct software_node *nodes)
{
	return -ENODEV;
}

void software_node_unregister_nodes(const struct software_node *nodes)
{
}

int software_node_register_node_group(const struct software_node **node_group)
{
	return -ENODEV;
}

void software_node_unregister_node_group(const struct software_node **node_group)
{
}

int software_node_register(const struct software_node *node)
{
	return -ENODEV;
}

void software_node_unregister(const struct software_node *node)
{
}

struct fwnode_handle *
software_node_fwnode(const struct software_node *node)
{
	return NULL;
}

void fwnode_remove_software_node(struct fwnode_handle *fwnode)
{
}

int device_add_software_node(struct device *dev, const struct software_node *node)
{
	return -ENODEV;
}

void device_remove_software_node(struct device *dev)
{
}

int device_create_managed_software_node(struct device *dev,
					 const struct property_entry *properties,
					 const struct software_node *parent)
{
	return -ENODEV;
}

void software_node_notify(struct device *dev)
{
}

void software_node_notify_remove(struct device *dev)
{
}
