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

static inline void of_core_init(void) { }
static inline struct device_node *of_find_matching_node(struct device_node *from, const struct of_device_id *matches) { return NULL; }
static inline const char *of_node_full_name(const struct device_node *np) { return "<no-node>"; }
static inline int of_device_is_available(const struct device_node *device) { return 0; }
static inline struct device_node *of_get_cpu_node(int cpu, unsigned int *thread) { return NULL; }
#endif  
