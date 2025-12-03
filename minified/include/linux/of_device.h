#ifndef _LINUX_OF_DEVICE_H
#define _LINUX_OF_DEVICE_H

#include <linux/cpu.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>  

#include <linux/of.h>
#include <linux/mod_devicetable.h>

struct device;


static inline int of_driver_match_device(struct device *dev, const struct device_driver *drv) { return 0; }
/* of_device_uevent, of_device_get_match_data, of_device_modalias, of_device_request_module,
   of_device_uevent_modalias, of_match_device, of_cpu_device_node_get, of_dma_configure_id,
   of_dma_configure removed - unused */

#endif  
