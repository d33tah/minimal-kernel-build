
#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H

#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/irqdomain.h>
#include <linux/device.h>
#include <linux/list.h>

#include <linux/property.h>
#include <linux/uuid.h>

#ifndef _LINUX
#define _LINUX
#endif

typedef void *acpi_handle;

#define acpi_disabled 1



struct fwnode_handle;



static inline void acpi_device_notify_remove(struct device *dev) { }

#endif	 
