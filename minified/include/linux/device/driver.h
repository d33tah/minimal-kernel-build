
#ifndef _DEVICE_DRIVER_H_
#define _DEVICE_DRIVER_H_

#include <linux/kobject.h>
#include <linux/pm.h>
#include <linux/device/bus.h>
#include <linux/module.h>

/* struct device_driver + enum probe_type removed - no driver ever registers on
   this build, the struct was never instantiated and dev->driver is never
   dispatched (only the forward decl in device.h is needed for the field type). */

void driver_init(void);

#endif	 
