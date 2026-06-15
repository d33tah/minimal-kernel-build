
#ifndef _PLATFORM_DEVICE_H_
#define _PLATFORM_DEVICE_H_

#include <linux/device.h>

/*
 * The platform bus is unused in this minimal kernel: no platform device is
 * ever registered and no platform driver ever registers, so the whole
 * subsystem (drivers/base/platform.c, the bus_type and its callbacks) was
 * removed. Only a forward declaration of struct platform_device is kept so
 * the decl-only externs in <linux/of_device.h> still parse.
 */
struct platform_device;

#endif
