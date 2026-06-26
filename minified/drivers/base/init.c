
#include <linux/device.h>
#include <linux/init.h>
#include <linux/memory.h>
#include <linux/of.h>

#include "base.h"


void __init driver_init(void)
{
	devices_init();
	buses_init();
	classes_init();
}
