#ifndef _LINUX_OF_DEVICE_H
#define _LINUX_OF_DEVICE_H

#include <linux/cpu.h>
#include <linux/platform_device.h>

/* Inlined from of_platform.h */
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/pm.h>

struct of_dev_auxdata {
	char *compatible;
	resource_size_t phys_addr;
	char *name;
	void *platform_data;
};

#define OF_DEV_AUXDATA(_compat,_phys,_name,_pdata) \
	{ .compatible = _compat, .phys_addr = _phys, .name = _name, \
	  .platform_data = _pdata }

extern const struct of_device_id of_default_bus_match_table[];

extern struct platform_device *of_device_alloc(struct device_node *np,
					 const char *bus_id,
					 struct device *parent);
static inline struct platform_device *of_find_device_by_node(struct device_node *np)
{ return NULL; }

extern struct platform_device *of_platform_device_create(struct device_node *np,
						   const char *bus_id,
						   struct device *parent);

extern int of_platform_device_destroy(struct device *dev, void *data);
extern int of_platform_bus_probe(struct device_node *root,
				 const struct of_device_id *matches,
				 struct device *parent);
static inline int of_platform_populate(struct device_node *root,
					const struct of_device_id *matches,
					const struct of_dev_auxdata *lookup,
					struct device *parent)
{ return -ENODEV; }
static inline int of_platform_default_populate(struct device_node *root,
					       const struct of_dev_auxdata *lookup,
					       struct device *parent)
{ return -ENODEV; }
static inline void of_platform_depopulate(struct device *parent) { }

static inline int devm_of_platform_populate(struct device *dev)
{ return -ENODEV; }

static inline void devm_of_platform_depopulate(struct device *dev) { }

static inline void of_platform_register_reconfig_notifier(void) { }
/* End of inlined of_platform.h content */

#include <linux/of.h>

struct device;


static inline int of_driver_match_device(struct device *dev, const struct device_driver *drv) { return 0; }
/* of_device_uevent, of_device_get_match_data, of_device_modalias, of_device_request_module,
   of_device_uevent_modalias, of_match_device, of_cpu_device_node_get, of_dma_configure_id,
   of_dma_configure removed - unused */

#endif
