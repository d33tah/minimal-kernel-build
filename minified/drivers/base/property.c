// SPDX-License-Identifier: GPL-2.0
/*
 * property.c - Unified device property interface (STUBBED)
 *
 * Minimal stub - device property/firmware node functionality not needed
 * for minimal kernel with basic TTY console support.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/property.h>
#include <linux/device.h>
#include <linux/errno.h>

struct fwnode_handle *dev_fwnode(struct device *dev)
{
	return NULL;
}

bool device_property_present(struct device *dev, const char *propname)
{
	return false;
}

bool fwnode_property_present(const struct fwnode_handle *fwnode,
			     const char *propname)
{
	return false;
}

int device_property_read_u8_array(struct device *dev, const char *propname,
				  u8 *val, size_t nval)
{
	return -ENXIO;
}

int device_property_read_u16_array(struct device *dev, const char *propname,
				   u16 *val, size_t nval)
{
	return -ENXIO;
}

int device_property_read_u32_array(struct device *dev, const char *propname,
				   u32 *val, size_t nval)
{
	return -ENXIO;
}

int device_property_read_u64_array(struct device *dev, const char *propname,
				   u64 *val, size_t nval)
{
	return -ENXIO;
}

int device_property_read_string_array(struct device *dev, const char *propname,
				      const char **val, size_t nval)
{
	return -ENXIO;
}

int device_property_read_string(struct device *dev, const char *propname,
				const char **val)
{
	return -ENXIO;
}

int device_property_match_string(struct device *dev, const char *propname,
				 const char *string)
{
	return -ENXIO;
}

int fwnode_property_read_u8_array(const struct fwnode_handle *fwnode,
				  const char *propname, u8 *val, size_t nval)
{
	return -ENXIO;
}

int fwnode_property_read_u16_array(const struct fwnode_handle *fwnode,
				   const char *propname, u16 *val, size_t nval)
{
	return -ENXIO;
}

int fwnode_property_read_u32_array(const struct fwnode_handle *fwnode,
				   const char *propname, u32 *val, size_t nval)
{
	return -ENXIO;
}

int fwnode_property_read_u64_array(const struct fwnode_handle *fwnode,
				   const char *propname, u64 *val, size_t nval)
{
	return -ENXIO;
}

int fwnode_property_read_string_array(const struct fwnode_handle *fwnode,
				      const char *propname, const char **val,
				      size_t nval)
{
	return -ENXIO;
}

int fwnode_property_read_string(const struct fwnode_handle *fwnode,
				const char *propname, const char **val)
{
	return -ENXIO;
}

int fwnode_property_match_string(const struct fwnode_handle *fwnode,
	const char *propname, const char *string)
{
	return -ENXIO;
}

int fwnode_property_get_reference_args(const struct fwnode_handle *fwnode,
				       const char *prop, const char *nargs_prop,
				       unsigned int nargs, unsigned int index,
				       struct fwnode_reference_args *args)
{
	return -ENOENT;
}

struct fwnode_handle *fwnode_find_reference(const struct fwnode_handle *fwnode,
					    const char *name,
					    unsigned int index)
{
	return ERR_PTR(-ENOENT);
}

const char *fwnode_get_name(const struct fwnode_handle *fwnode)
{
	return NULL;
}

const char *fwnode_get_name_prefix(const struct fwnode_handle *fwnode)
{
	return NULL;
}

struct fwnode_handle *fwnode_get_parent(const struct fwnode_handle *fwnode)
{
	return NULL;
}

struct fwnode_handle *fwnode_get_next_parent(struct fwnode_handle *fwnode)
{
	return NULL;
}

struct device *fwnode_get_next_parent_dev(struct fwnode_handle *fwnode)
{
	return NULL;
}

unsigned int fwnode_count_parents(const struct fwnode_handle *fwnode)
{
	return 0;
}

struct fwnode_handle *fwnode_get_nth_parent(struct fwnode_handle *fwnode,
					    unsigned int depth)
{
	return NULL;
}

bool fwnode_is_ancestor_of(struct fwnode_handle *ancestor, struct fwnode_handle *child)
{
	return false;
}

struct fwnode_handle *
fwnode_get_next_child_node(const struct fwnode_handle *fwnode,
			   struct fwnode_handle *child)
{
	return NULL;
}

struct fwnode_handle *
fwnode_get_next_available_child_node(const struct fwnode_handle *fwnode,
				     struct fwnode_handle *child)
{
	return NULL;
}

struct fwnode_handle *device_get_next_child_node(struct device *dev,
						 struct fwnode_handle *child)
{
	return NULL;
}

struct fwnode_handle *
fwnode_get_named_child_node(const struct fwnode_handle *fwnode,
			    const char *childname)
{
	return NULL;
}

struct fwnode_handle *device_get_named_child_node(struct device *dev,
						  const char *childname)
{
	return NULL;
}

struct fwnode_handle *fwnode_handle_get(struct fwnode_handle *fwnode)
{
	return fwnode;
}

void fwnode_handle_put(struct fwnode_handle *fwnode)
{
}

bool fwnode_device_is_available(const struct fwnode_handle *fwnode)
{
	return false;
}

unsigned int device_get_child_node_count(struct device *dev)
{
	return 0;
}

bool device_dma_supported(struct device *dev)
{
	return false;
}

enum dev_dma_attr device_get_dma_attr(struct device *dev)
{
	return DEV_DMA_NOT_SUPPORTED;
}

int fwnode_get_phy_mode(struct fwnode_handle *fwnode)
{
	return -ENODEV;
}

int device_get_phy_mode(struct device *dev)
{
	return -ENODEV;
}

void __iomem *fwnode_iomap(struct fwnode_handle *fwnode, int index)
{
	return NULL;
}

int fwnode_irq_get(const struct fwnode_handle *fwnode, unsigned int index)
{
	return -ENXIO;
}

int fwnode_irq_get_byname(const struct fwnode_handle *fwnode, const char *name)
{
	return -ENXIO;
}

struct fwnode_handle *
fwnode_graph_get_next_endpoint(const struct fwnode_handle *fwnode,
			       struct fwnode_handle *prev)
{
	return NULL;
}

struct fwnode_handle *
fwnode_graph_get_port_parent(const struct fwnode_handle *endpoint)
{
	return NULL;
}

struct fwnode_handle *
fwnode_graph_get_remote_port_parent(const struct fwnode_handle *fwnode)
{
	return NULL;
}

struct fwnode_handle *
fwnode_graph_get_remote_port(const struct fwnode_handle *fwnode)
{
	return NULL;
}

struct fwnode_handle *
fwnode_graph_get_remote_endpoint(const struct fwnode_handle *fwnode)
{
	return NULL;
}

struct fwnode_handle *
fwnode_graph_get_endpoint_by_id(const struct fwnode_handle *fwnode,
				u32 port, u32 endpoint, unsigned long flags)
{
	return NULL;
}

unsigned int fwnode_graph_get_endpoint_count(struct fwnode_handle *fwnode,
					     unsigned long flags)
{
	return 0;
}

int fwnode_graph_parse_endpoint(const struct fwnode_handle *fwnode,
				struct fwnode_endpoint *endpoint)
{
	return -ENXIO;
}

const void *device_get_match_data(struct device *dev)
{
	return NULL;
}

void *fwnode_connection_find_match(struct fwnode_handle *fwnode,
				   const char *con_id, void *data,
				   devcon_match_fn_t match)
{
	return NULL;
}

int fwnode_connection_find_matches(struct fwnode_handle *fwnode,
				   const char *con_id, void *data,
				   devcon_match_fn_t match,
				   void **matches, unsigned int matches_len)
{
	return -EINVAL;
}
