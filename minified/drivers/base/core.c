
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
/* linux/module.h removed - no module features used */
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/of.h>
/* of_device.h removed - only has includes, no definitions used */
/* linux/blkdev.h removed - empty stub */
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/sysfs.h>
#include <linux/backing-dev.h>

#include "base.h"

/* Merged from init.c */
void __init driver_init(void)
{
	bdi_init(&noop_backing_dev_info);
	devices_init();
	buses_init();
	classes_init();
}

/* Removed: fwnode_link_add, fwnode_links_purge, fw_devlink_purge_absent_suppliers - no callers */

/* bool dma_default_coherent removed - never used */
/* device_links_srcu removed - never used */

/* device_pm_move_to_tail removed - was empty stub (lock/unlock with no work) */

/* device_ktype, device_release, dev_sysfs_ops, dev_attr_show/store removed - unused */

/* Stub: online sysfs attributes simplified for minimal kernel */

/* device_add_groups, device_remove_groups removed - calls removed, functions did nothing */

/* device_remove_attrs removed - physical_location field removed, now empty */

/* device_remove_file_self, device_create_bin_file, device_remove_bin_file - no callers */

struct kset *devices_kset;

/* klist_children_get/put, device_initialize, dev_set_name, device_add, device_register removed - no callers */

static DEFINE_MUTEX(gdp_mutex);

struct device *get_device(struct device *dev)
{
	return dev ? kobj_to_dev(kobject_get(&dev->kobj)) : NULL;
}

void put_device(struct device *dev)
{
	if (dev)
		kobject_put(&dev->kobj);
}

/* kill_device inlined - sets dev->p->dead = true */

/* Simplified - kobject_create_and_add returns NULL (stub), return ignored */
int __init devices_init(void)
{
	devices_kset = kset_create_and_add("devices", NULL);
	return 0;
}

/* Removed: root_device struct, __root_device_register, root_device_unregister - no callers */

/* device_create_release, device_create_groups_vargs, device_create,
   device_destroy removed - no callers */

/* dev_err_probe, device_match_devt removed - never called */
