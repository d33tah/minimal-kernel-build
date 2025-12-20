#ifndef _LINUX_FWNODE_H_
#define _LINUX_FWNODE_H_

#include <linux/types.h>

struct device;

/* Minimal fwnode_handle - embedded in device_node */
struct fwnode_handle {
	struct fwnode_handle *secondary;
	struct device *dev;
};

#endif
