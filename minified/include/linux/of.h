 
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
static inline struct device_node *of_find_node_by_name(struct device_node *from, const char *name) { return NULL; }
static inline struct device_node *of_find_node_by_type(struct device_node *from, const char *type) { return NULL; }
static inline struct device_node *of_find_matching_node_and_match(struct device_node *from, const struct of_device_id *matches, const struct of_device_id **match) { return NULL; }
static inline struct device_node *of_find_matching_node(struct device_node *from, const struct of_device_id *matches) { return NULL; }
static inline struct device_node *of_find_node_by_path(const char *path) { return NULL; }
static inline struct device_node *of_find_node_opts_by_path(const char *path, const char **opts) { return NULL; }
static inline struct device_node *of_find_node_by_phandle(phandle handle) { return NULL; }
static inline struct device_node *of_get_parent(const struct device_node *node) { return NULL; }
static inline struct device_node *of_get_next_parent(struct device_node *node) { return NULL; }
static inline struct device_node *of_get_next_child(const struct device_node *node, struct device_node *prev) { return NULL; }
static inline struct device_node *of_get_next_available_child(const struct device_node *node, struct device_node *prev) { return NULL; }
static inline struct device_node *of_find_node_with_property(struct device_node *from, const char *prop_name) { return NULL; }
static inline bool of_node_is_root(const struct device_node *node) { return false; }
static inline struct device_node *of_find_all_nodes(struct device_node *prev) { return NULL; }
static inline const char *of_node_full_name(const struct device_node *np) { return "<no-node>"; }
static inline struct device_node *of_find_compatible_node(struct device_node *from, const char *type, const char *compatible) { return NULL; }
static inline int of_property_count_elems_of_size(const struct device_node *np, const char *propname, int elem_size) { return -ENOSYS; }
static inline int of_property_read_u32_index(const struct device_node *np, const char *propname, u32 index, u32 *out_value) { return -ENOSYS; }
static inline int of_property_read_u8_array(const struct device_node *np, const char *propname, u8 *out_values, size_t sz) { return -ENOSYS; }
static inline int of_property_read_u16_array(const struct device_node *np, const char *propname, u16 *out_values, size_t sz) { return -ENOSYS; }
static inline int of_property_read_u32_array(const struct device_node *np, const char *propname, u32 *out_values, size_t sz) { return -ENOSYS; }
static inline int of_property_read_u64_array(const struct device_node *np, const char *propname, u64 *out_values, size_t sz) { return -ENOSYS; }
static inline int of_property_read_string(const struct device_node *np, const char *propname, const char **out_string) { return -ENOSYS; }
static inline int of_property_read_string_helper(const struct device_node *np, const char *propname, const char **out_strs, size_t sz, int skip) { return -ENOSYS; }
static inline int of_device_is_compatible(const struct device_node *device, const char *compat) { return 0; }
static inline int of_device_is_available(const struct device_node *device) { return 0; }
static inline bool of_device_is_big_endian(const struct device_node *device) { return false; }
static inline struct property *of_find_property(const struct device_node *np, const char *name, int *lenp) { return NULL; }
static inline bool of_console_check(struct device_node *dn, char *name, int index) { return false; }
static inline const __be32 *of_prop_next_u32(struct property *prop, const __be32 *cur, u32 *pu) { return NULL; }
static inline const char *of_prop_next_string(struct property *prop, const char *cur) { return NULL; }
static inline int of_node_check_flag(struct device_node *n, unsigned long flag) { return 0; }
static inline int of_node_test_and_set_flag(struct device_node *n, unsigned long flag) { return 0; }
static inline void of_node_set_flag(struct device_node *n, unsigned long flag) { }
static inline void of_node_clear_flag(struct device_node *n, unsigned long flag) { }
static inline int of_property_read_u64(const struct device_node *np, const char *propname, u64 *out_value) { return -ENOSYS; }
static inline int of_property_match_string(const struct device_node *np, const char *propname, const char *string) { return -ENOSYS; }
static inline struct device_node *of_parse_phandle(const struct device_node *np, const char *phandle_name, int index) { return NULL; }
static inline int of_parse_phandle_with_args(const struct device_node *np, const char *list_name, const char *cells_name, int cell_count, int index, struct of_phandle_args *out_args) { return -ENOSYS; }
static inline int of_parse_phandle_with_fixed_args(const struct device_node *np, const char *list_name, int cell_count, int index, struct of_phandle_args *out_args) { return -ENOSYS; }
static inline int of_count_phandle_with_args(const struct device_node *np, const char *list_name, const char *cells_name) { return -ENOSYS; }
static inline int of_alias_get_id(struct device_node *np, const char *stem) { return -ENOSYS; }
static inline int of_alias_get_highest_id(const char *stem) { return -ENOSYS; }
static inline int of_machine_is_compatible(const char *compat) { return 0; }
static inline bool of_property_read_bool(const struct device_node *np, const char *propname) { return false; }
static inline int of_property_read_u8(const struct device_node *np, const char *propname, u8 *out_value) { return -ENOSYS; }
static inline int of_property_read_u16(const struct device_node *np, const char *propname, u16 *out_value) { return -ENOSYS; }
static inline int of_property_read_u32(const struct device_node *np, const char *propname, u32 *out_value) { return -ENOSYS; }
static inline int of_property_read_s32(const struct device_node *np, const char *propname, s32 *out_value) { return -ENOSYS; }
static inline struct device_node *of_get_cpu_node(int cpu, unsigned int *thread) { return NULL; }
#endif  
