#include <linux/device.h>
#include <linux/init.h>
static inline void memory_dev_init(void)
{
}
#include <linux/of.h>
#include <linux/backing-dev.h>
#include "base.h"
static inline void node_dev_init(void)
{
}
void __init driver_init(void)
{
	bdi_init(&noop_backing_dev_info);
	devtmpfs_init();
	devices_init();
	buses_init();
	classes_init();
	firmware_init();
	hypervisor_init();
	of_core_init();
	platform_bus_init();
	auxiliary_bus_init();
	cpu_dev_init();
	memory_dev_init();
	node_dev_init();
	container_dev_init();
}
