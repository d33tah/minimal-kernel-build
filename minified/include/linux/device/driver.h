
#ifndef _DEVICE_DRIVER_H_
#define _DEVICE_DRIVER_H_

#include <linux/kobject.h>
#include <linux/klist.h>
#include <linux/pm.h>
#include <linux/device/bus.h>
#include <linux/module.h>

/* probe_type enum removed - never read */

struct device_driver {
	const char		*name;
	struct bus_type		*bus;

	struct module		*owner;
	/* mod_name, suppress_bind_attrs, probe_type, of_match_table, acpi_match_table removed - unused */

	int (*probe) (struct device *dev);
	/* sync_state, suspend, resume, pm, coredump removed - never called */
	int (*remove) (struct device *dev);
	void (*shutdown) (struct device *dev);
	/* groups, dev_groups removed - never read (sysfs stubbed) */

	struct driver_private *p;
};


/* driver_register removed - never called */
/* driver_find removed - kset_find_obj always returns NULL */
/* driver_probe_done removed - never called */
extern void wait_for_device_probe(void);


/* struct driver_attribute removed - never used */

/* DRIVER_ATTR_RW, DRIVER_ATTR_RO, DRIVER_ATTR_WO, driver_create_file, driver_remove_file, driver_find_device,
   driver_deferred_probe_timeout, driver_deferred_probe_add removed - only used internally in dd.c */
void driver_init(void);

#endif	 
