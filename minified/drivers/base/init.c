#include <linux/device.h>
#include <linux/init.h>
/* of.h removed - unused */
#include <linux/backing-dev.h>
#include "base.h"
void __init driver_init(void)
{
	bdi_init(&noop_backing_dev_info);
	/* devtmpfs_init removed - empty stub */
	devices_init();
	buses_init();
	classes_init();
	/* firmware_init, hypervisor_init, of_core_init removed - empty stubs */
	platform_bus_init();
	cpu_dev_init();
	/* auxiliary_bus_init, memory_dev_init, node_dev_init, container_dev_init removed - empty stubs */
}
