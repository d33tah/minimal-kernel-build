#ifndef _LINUX_OF_H
#define _LINUX_OF_H
#include <linux/types.h>
struct device;
struct fwnode_handle { struct fwnode_handle *secondary; struct device *dev; };
/* phandle typedef inlined into device_node */
struct property { char *name; int length; void *value; struct property *next; };
struct device_node { const char *name; u32 phandle; const char *full_name; struct fwnode_handle fwnode; struct property *properties; struct property *deadprops; struct device_node *parent; struct device_node *child; struct device_node *sibling; unsigned long _flags; void *data; };
#endif  
