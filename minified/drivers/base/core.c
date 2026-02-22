/* Simplified - only bdi_init needed for hello-world kernel */
#include <linux/slab.h>
#include <linux/backing-dev.h>

#include "base.h"

void __init driver_init(void)
{
	bdi_init(&noop_backing_dev_info);
}

struct kset *devices_kset;

struct device *get_device(struct device *dev)
{
	return dev;
}

void put_device(struct device *dev)
{
}

int __init devices_init(void)
{
	return 0;
}
