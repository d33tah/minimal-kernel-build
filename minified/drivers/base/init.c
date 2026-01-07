#include <linux/device.h>
#include <linux/init.h>
#include <linux/backing-dev.h>
#include "base.h"
/* didbg debug function removed - not needed for production */
void __init driver_init(void)
{
	bdi_init(&noop_backing_dev_info);
	devices_init();
	buses_init();
	classes_init();
}
