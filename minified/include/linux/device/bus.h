
#ifndef _DEVICE_BUS_H_
#define _DEVICE_BUS_H_

#include <linux/kobject.h>
#include <linux/klist.h>

struct bus_type {
	/* All other fields removed - no bus is ever registered and the struct is
	   never instantiated; the only field ever dispatched is dev_name, via the
	   (always-NULL on this build) dev->bus->dev_name read in core.c. */
	const char		*dev_name;
};

/* device_match_name, device_match_of_node, device_match_fwnode,
   device_match_acpi_dev, device_match_acpi_handle, device_match_any removed - unused */
int device_match_devt(struct device *dev, const void *pdevt);


#endif
