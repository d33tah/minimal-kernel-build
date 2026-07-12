
#include <linux/device.h>

#include "base.h"


void __init driver_init(void) {
	devices_init();
	classes_init(); }
