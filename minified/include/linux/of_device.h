#ifndef _LINUX_OF_DEVICE_H
#define _LINUX_OF_DEVICE_H
#include <linux/cpu.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/pm.h>
struct of_dev_auxdata { char *compatible; resource_size_t phys_addr; char *name; void *platform_data; };
#define OF_DEV_AUXDATA(_compat,_phys,_name,_pdata) { .compatible = _compat, .phys_addr = _phys, .name = _name, .platform_data = _pdata }
static inline struct platform_device *of_find_device_by_node(struct device_node *np) { return NULL; }
static inline void of_platform_register_reconfig_notifier(void) { }
#include <linux/of.h>
struct device;
static inline int of_driver_match_device(struct device *dev, const struct device_driver *drv) { return 0; }
#endif
