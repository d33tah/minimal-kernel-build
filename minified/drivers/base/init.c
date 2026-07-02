
#include <linux/device.h>
#include <linux/init.h>

#include "base.h"


void __init driver_init(void)
{
	devices_init();
	classes_init();
}
