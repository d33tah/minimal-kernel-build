#include <linux/device.h>
#include <linux/init.h>
/* of.h removed - unused */
#include <linux/backing-dev.h>
#include "base.h"
static inline void didbg(const char *s)
{
	while (*s)
		asm volatile("outb %0, $0xe9" : : "a"(*s++));
}
void __init driver_init(void)
{
	didbg("di:bdi\n");
	bdi_init(&noop_backing_dev_info);
	/* devtmpfs_init removed - empty stub */
	didbg("di:devices\n");
	devices_init();
	didbg("di:buses\n");
	buses_init();
	didbg("di:classes\n");
	classes_init();
	/* firmware_init, hypervisor_init, of_core_init removed - empty stubs */
	/* platform_bus_init removed - not needed for minimal Hello World */
	/* cpu_dev_init removed - bus_register hangs with low memory */
	didbg("di:done\n");
	/* auxiliary_bus_init, memory_dev_init, node_dev_init, container_dev_init removed - empty stubs */
}
