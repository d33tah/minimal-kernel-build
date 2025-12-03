
#ifndef _LINUX_PROPERTY_H_
#define _LINUX_PROPERTY_H_

#include <linux/bits.h>
#include <linux/fwnode.h>
#include <linux/types.h>

struct device;
struct net_device;

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

struct fwnode_handle *dev_fwnode(struct device *dev);

bool device_property_present(struct device *dev, const char *propname);
int device_property_read_u8_array(struct device *dev, const char *propname,
				  u8 *val, size_t nval);
int device_property_read_u16_array(struct device *dev, const char *propname,
				   u16 *val, size_t nval);
int device_property_read_u32_array(struct device *dev, const char *propname,
				   u32 *val, size_t nval);
int device_property_read_u64_array(struct device *dev, const char *propname,
				   u64 *val, size_t nval);
int device_property_read_string_array(struct device *dev, const char *propname,
				      const char **val, size_t nval);
int device_property_read_string(struct device *dev, const char *propname,
				const char **val);
int device_property_match_string(struct device *dev,
				 const char *propname, const char *string);

bool fwnode_device_is_available(const struct fwnode_handle *fwnode);
bool fwnode_property_present(const struct fwnode_handle *fwnode,
			     const char *propname);
int fwnode_property_read_u8_array(const struct fwnode_handle *fwnode,
				  const char *propname, u8 *val,
				  size_t nval);
int fwnode_property_read_u16_array(const struct fwnode_handle *fwnode,
				   const char *propname, u16 *val,
				   size_t nval);
int fwnode_property_read_u32_array(const struct fwnode_handle *fwnode,
				   const char *propname, u32 *val,
				   size_t nval);
int fwnode_property_read_u64_array(const struct fwnode_handle *fwnode,
				   const char *propname, u64 *val,
				   size_t nval);
int fwnode_property_read_string_array(const struct fwnode_handle *fwnode,
				      const char *propname, const char **val,
				      size_t nval);
int fwnode_property_read_string(const struct fwnode_handle *fwnode,
				const char *propname, const char **val);
int fwnode_property_match_string(const struct fwnode_handle *fwnode,
				 const char *propname, const char *string);
int fwnode_property_get_reference_args(const struct fwnode_handle *fwnode,
				       const char *prop, const char *nargs_prop,
				       unsigned int nargs, unsigned int index,
				       struct fwnode_reference_args *args);

struct fwnode_handle *fwnode_find_reference(const struct fwnode_handle *fwnode,
					    const char *name,
					    unsigned int index);

const char *fwnode_get_name(const struct fwnode_handle *fwnode);
const char *fwnode_get_name_prefix(const struct fwnode_handle *fwnode);

struct fwnode_handle *fwnode_get_parent(const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_get_next_parent(struct fwnode_handle *fwnode);

#define fwnode_for_each_parent_node(fwnode, parent)		\
	for (parent = fwnode_get_parent(fwnode); parent;	\
	     parent = fwnode_get_next_parent(parent))

struct device *fwnode_get_next_parent_dev(struct fwnode_handle *fwnode);
unsigned int fwnode_count_parents(const struct fwnode_handle *fwn);
struct fwnode_handle *fwnode_get_nth_parent(struct fwnode_handle *fwn,
					    unsigned int depth);
bool fwnode_is_ancestor_of(struct fwnode_handle *ancestor, struct fwnode_handle *child);
struct fwnode_handle *fwnode_get_next_child_node(
	const struct fwnode_handle *fwnode, struct fwnode_handle *child);
struct fwnode_handle *fwnode_get_next_available_child_node(
	const struct fwnode_handle *fwnode, struct fwnode_handle *child);

#define fwnode_for_each_child_node(fwnode, child)			\
	for (child = fwnode_get_next_child_node(fwnode, NULL); child;	\
	     child = fwnode_get_next_child_node(fwnode, child))

#define fwnode_for_each_available_child_node(fwnode, child)		       \
	for (child = fwnode_get_next_available_child_node(fwnode, NULL); child;\
	     child = fwnode_get_next_available_child_node(fwnode, child))

struct fwnode_handle *device_get_next_child_node(
	struct device *dev, struct fwnode_handle *child);

#define device_for_each_child_node(dev, child)				\
	for (child = device_get_next_child_node(dev, NULL); child;	\
	     child = device_get_next_child_node(dev, child))

struct fwnode_handle *fwnode_get_named_child_node(
	const struct fwnode_handle *fwnode, const char *childname);
struct fwnode_handle *device_get_named_child_node(struct device *dev,
						  const char *childname);

struct fwnode_handle *fwnode_handle_get(struct fwnode_handle *fwnode);
void fwnode_handle_put(struct fwnode_handle *fwnode);

int fwnode_irq_get(const struct fwnode_handle *fwnode, unsigned int index);
int fwnode_irq_get_byname(const struct fwnode_handle *fwnode, const char *name);

unsigned int device_get_child_node_count(struct device *dev);

struct software_node;

struct software_node_ref_args {
	const struct software_node *node;
	unsigned int nargs;
	u64 args[NR_FWNODE_REFERENCE_ARGS];
};

/* SOFTWARE_NODE_REFERENCE removed - unused */

struct property_entry {
	const char *name;
	size_t length;
	bool is_inline;
	enum dev_prop_type type;
	union {
		const void *pointer;
		union {
			u8 u8_data[sizeof(u64) / sizeof(u8)];
			u16 u16_data[sizeof(u64) / sizeof(u16)];
			u32 u32_data[sizeof(u64) / sizeof(u32)];
			u64 u64_data[sizeof(u64) / sizeof(u64)];
			const char *str[sizeof(u64) / sizeof(char *)];
		} value;
	};
};
/* PROPERTY_ENTRY_* macros removed - unused */

struct property_entry *
property_entries_dup(const struct property_entry *properties);

void property_entries_free(const struct property_entry *properties);

bool device_dma_supported(struct device *dev);

enum dev_dma_attr device_get_dma_attr(struct device *dev);

const void *device_get_match_data(struct device *dev);

int device_get_phy_mode(struct device *dev);
int fwnode_get_phy_mode(struct fwnode_handle *fwnode);

void __iomem *fwnode_iomap(struct fwnode_handle *fwnode, int index);

struct fwnode_handle *fwnode_graph_get_next_endpoint(
	const struct fwnode_handle *fwnode, struct fwnode_handle *prev);
struct fwnode_handle *
fwnode_graph_get_port_parent(const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_graph_get_remote_port_parent(
	const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_graph_get_remote_port(
	const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_graph_get_remote_endpoint(
	const struct fwnode_handle *fwnode);


#define FWNODE_GRAPH_ENDPOINT_NEXT	BIT(0)
#define FWNODE_GRAPH_DEVICE_DISABLED	BIT(1)

struct fwnode_handle *
fwnode_graph_get_endpoint_by_id(const struct fwnode_handle *fwnode,
				u32 port, u32 endpoint, unsigned long flags);
unsigned int fwnode_graph_get_endpoint_count(struct fwnode_handle *fwnode,
					     unsigned long flags);

#define fwnode_graph_for_each_endpoint(fwnode, child)			\
	for (child = NULL;						\
	     (child = fwnode_graph_get_next_endpoint(fwnode, child)); )

int fwnode_graph_parse_endpoint(const struct fwnode_handle *fwnode,
				struct fwnode_endpoint *endpoint);

typedef void *(*devcon_match_fn_t)(struct fwnode_handle *fwnode, const char *id,
				   void *data);

void *fwnode_connection_find_match(struct fwnode_handle *fwnode,
				   const char *con_id, void *data,
				   devcon_match_fn_t match);

int fwnode_connection_find_matches(struct fwnode_handle *fwnode,
				   const char *con_id, void *data,
				   devcon_match_fn_t match,
				   void **matches, unsigned int matches_len);


struct software_node {
	const char *name;
	const struct software_node *parent;
	const struct property_entry *properties;
};

bool is_software_node(const struct fwnode_handle *fwnode);
const struct software_node *
to_software_node(const struct fwnode_handle *fwnode);
struct fwnode_handle *software_node_fwnode(const struct software_node *node);

const struct software_node *
software_node_find_by_name(const struct software_node *parent,
			   const char *name);

int software_node_register_nodes(const struct software_node *nodes);
void software_node_unregister_nodes(const struct software_node *nodes);

int software_node_register_node_group(const struct software_node **node_group);
void software_node_unregister_node_group(const struct software_node **node_group);

int software_node_register(const struct software_node *node);
void software_node_unregister(const struct software_node *node);

struct fwnode_handle *
fwnode_create_software_node(const struct property_entry *properties,
			    const struct fwnode_handle *parent);
void fwnode_remove_software_node(struct fwnode_handle *fwnode);

int device_add_software_node(struct device *dev, const struct software_node *node);
void device_remove_software_node(struct device *dev);

int device_create_managed_software_node(struct device *dev,
					const struct property_entry *properties,
					const struct software_node *parent);

#endif  
