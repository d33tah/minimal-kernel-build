#ifndef _LINUX_OF_DEVICE_H
#define _LINUX_OF_DEVICE_H
#include <linux/cpu.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/pm.h>
/* of_find_device_by_node removed - unused */
static inline void of_platform_register_reconfig_notifier(void) { }
#include <linux/of.h>
struct device;
static inline int of_driver_match_device(struct device *dev, const struct device_driver *drv) { return 0; }
#endif
