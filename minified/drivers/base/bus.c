/* Stubbed - no buses or device probing in hello-world kernel */
#include <linux/device.h>
#include "base.h"

int bus_for_each_drv(struct bus_type *bus, struct device_driver *start,
		     void *data, int (*fn)(struct device_driver *, void *))
{
	return 0;
}

void bus_probe_device(struct device *dev)
{
}

int __init buses_init(void)
{
	return 0;
}
