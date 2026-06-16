
#include <linux/device.h>
#include <linux/init.h>
#include <linux/memory.h>
#include <linux/of.h>
#include <linux/backing-dev.h>

#include "base.h"


void __init driver_init(void)
{
	 
	bdi_init(&noop_backing_dev_info);
	devices_init();
	buses_init();
	classes_init();


	of_core_init();
	memory_dev_init();
	node_dev_init();
}
