
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
	/* sync_state removed - never called */
	int (*remove) (struct device *dev);
	void (*shutdown) (struct device *dev);
	int (*suspend) (struct device *dev, pm_message_t state);
	int (*resume) (struct device *dev);
	/* groups, dev_groups removed - never read (sysfs stubbed) */

	const struct dev_pm_ops *pm;
	/* coredump removed - never called */

	struct driver_private *p;
};


extern int __must_check driver_register(struct device_driver *drv);

/* driver_find removed - kset_find_obj always returns NULL */
extern int driver_probe_done(void);
extern void wait_for_device_probe(void);


struct driver_attribute {
	struct attribute attr;
	ssize_t (*show)(struct device_driver *driver, char *buf);
	ssize_t (*store)(struct device_driver *driver, const char *buf,
			 size_t count);
};

/* DRIVER_ATTR_RW, DRIVER_ATTR_RO, DRIVER_ATTR_WO, driver_create_file, driver_remove_file, driver_find_device removed - unused */

extern int driver_deferred_probe_timeout;
void driver_deferred_probe_add(struct device *dev);
void driver_init(void);

#endif	 
