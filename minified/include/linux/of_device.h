#ifndef _LINUX_OF_DEVICE_H
#define _LINUX_OF_DEVICE_H

#include <linux/cpu.h>
#include <linux/platform_device.h>

/* Inlined from of_platform.h */
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/pm.h>

extern const struct of_device_id of_default_bus_match_table[];

extern struct platform_device *of_device_alloc(struct device_node *np,
					 const char *bus_id,
					 struct device *parent);

extern struct platform_device *of_platform_device_create(struct device_node *np,
						   const char *bus_id,
						   struct device *parent);

extern int of_platform_bus_probe(struct device_node *root,
				 const struct of_device_id *matches,
				 struct device *parent);
static inline void of_platform_register_reconfig_notifier(void) { }

#include <linux/of.h>

struct device;

static inline int of_driver_match_device(struct device *dev, const struct device_driver *drv) { return 0; }
/* of_device_uevent, of_device_get_match_data, of_device_modalias, of_device_request_module,
   of_device_uevent_modalias, of_match_device, of_cpu_device_node_get, of_dma_configure_id,
   of_dma_configure removed - unused */

#endif
