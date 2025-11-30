 
#ifndef _LINUX_OF_H
#define _LINUX_OF_H
 

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>

typedef u32 phandle;
typedef u32 ihandle;

struct property {
	char	*name;
	int	length;
	void	*value;
	struct property *next;
};

struct device_node {
	const char *name;
	phandle phandle;
	const char *full_name;
	struct fwnode_handle fwnode;
	struct	property *properties;
	struct	property *deadprops;
	struct	device_node *parent;
	struct	device_node *child;
	struct	device_node *sibling;
	unsigned long _flags;
	void	*data;
};

#define MAX_PHANDLE_ARGS 16
struct of_phandle_args {
	struct device_node *np;
	int args_count;
	uint32_t args[MAX_PHANDLE_ARGS];
};

 
static inline struct device_node *of_node_get(struct device_node *node) { return node; }
static inline void of_node_put(struct device_node *node) { }

 
extern struct device_node *of_root;
extern struct device_node *of_chosen;
extern struct device_node *of_aliases;
extern struct device_node *of_stdout;

#define of_node_kobj(n) NULL

#define OF_DYNAMIC		1
#define OF_DETACHED		2
#define OF_POPULATED		3
#define OF_POPULATED_BUS	4
#define OF_OVERLAY		5
#define OF_OVERLAY_FREE_CSET	6
#define OF_BAD_ADDR	((u64)-1)

 
static inline void of_core_init(void) { }
static inline struct device_node *to_of_node(const struct fwnode_handle *fwnode) { return NULL; }
/* of_find_node_by_name, of_find_node_by_type removed - unused */
/* of_find_matching_node_and_match removed - unused */
static inline struct device_node *of_find_matching_node(struct device_node *from, const struct of_device_id *matches) { return NULL; }
/* of_find_node_by_path, of_find_node_opts_by_path, of_find_node_by_phandle removed - unused */
/* of_get_parent, of_get_next_parent, of_get_next_child, of_get_next_available_child removed - unused */
/* of_find_node_with_property, of_node_is_root, of_find_all_nodes removed - unused */
static inline const char *of_node_full_name(const struct device_node *np) { return "<no-node>"; }
/* of_find_compatible_node removed - unused */
/* of_property_count_elems_of_size, of_property_read_u32_index removed - unused */
/* of_property_read_*_array, of_property_read_string* removed - unused */
/* of_device_is_compatible removed - unused */
static inline int of_device_is_available(const struct device_node *device) { return 0; }
/* of_device_is_big_endian, of_find_property, of_console_check removed - unused */
/* of_prop_next_*, of_node_*_flag removed - unused */
/* of_property_read_u64, of_property_match_string removed - unused */
/* of_parse_phandle* removed - unused */
/* of_count_phandle_with_args, of_alias_get_id, of_alias_get_highest_id removed - unused */
/* of_machine_is_compatible, of_property_read_bool removed - unused */
/* of_property_read_u8/u16/u32/s32 removed - unused */
static inline struct device_node *of_get_cpu_node(int cpu, unsigned int *thread) { return NULL; }
#endif  
