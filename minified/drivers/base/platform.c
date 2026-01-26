
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include <linux/types.h>

#include "base.h"
#include "power/power.h"

/* struct device platform_bus removed - never used */

/* platform_get_mem_or_io, devm_platform_get_and_ioremap_resource,
   devm_platform_ioremap_resource, devm_platform_ioremap_resource_byname,
   platform_get_irq_optional, struct platform_object,
   platform_device_add_resources, platform_device_add_data, platform_device_add,
   platform_device_del, platform_device_register, platform_device_unregister,
   platform_device_register_full removed - no external callers */

int __platform_driver_register(struct platform_driver *drv,
			       struct module *owner)
{
	drv->driver.owner = owner;
	drv->driver.bus = &platform_bus_type;

	return driver_register(&drv->driver);
}

/* Removed: __platform_driver_probe, __platform_create_bundle,
   __platform_register_drivers, platform_unregister_drivers - no external callers */
/* platform_match_id inlined into platform_match */

/* platform_dev_attrs, platform_dev_group removed - dev_groups field never read */

static int platform_match(struct device *dev, struct device_driver *drv)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct platform_driver *pdrv = to_platform_driver(drv);

	if (pdev->driver_override)
		return !strcmp(pdev->driver_override, drv->name);

	if (pdrv->id_table) {
		/* platform_match_id inlined */
		const struct platform_device_id *id = pdrv->id_table;
		while (id->name[0]) {
			if (strcmp(pdev->name, id->name) == 0) {
				pdev->id_entry = id;
				return 1;
			}
			id++;
		}
		return 0;
	}

	return (strcmp(pdev->name, drv->name) == 0);
}

/* platform_uevent removed - uevent callback removed from bus_type */

static int platform_probe(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);
	int ret = 0;

	if (drv->probe)
		ret = drv->probe(dev);

	if (drv->prevent_deferred_probe && ret == -EPROBE_DEFER)
		ret = -ENXIO;

	return ret;
}

static void platform_remove(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);

	if (drv->remove)
		drv->remove(dev);
}

static void platform_shutdown(struct device *_dev)
{
	struct platform_device *dev = to_platform_device(_dev);
	struct platform_driver *drv;

	if (!_dev->driver)
		return;

	drv = to_platform_driver(_dev->driver);
	if (drv->shutdown)
		drv->shutdown(dev);
}

/* Stub: DMA configure/cleanup not needed for minimal kernel */
static int platform_dma_configure(struct device *dev)
{
	return 0;
}

/* platform_dev_pm_ops removed - pm field removed from bus_type */

struct bus_type platform_bus_type = {
	.name = "platform",
	/* .dev_groups, .pm, .uevent removed - fields never read/accessed (sysfs stubbed) */
	.match = platform_match,
	.probe = platform_probe,
	.remove = platform_remove,
	.shutdown = platform_shutdown,
	.dma_configure = platform_dma_configure,
};

/* Merged from driver.c */
int driver_register(struct device_driver *drv)
{
	int ret;

	if (!drv->bus->p) {
		pr_err("Driver '%s' was unable to register with bus_type '%s' because the bus was not initialized.\n",
		       drv->name, drv->bus->name);
		return -EINVAL;
	}

	/* driver_find always returns NULL (kset_find_obj stubbed) */

	ret = bus_add_driver(drv);
	if (ret)
		return ret;
	/* driver_add_groups call removed - was a stub returning 0 */
	kobject_uevent(&drv->p->kobj, KOBJ_ADD);
	deferred_probe_extend_timeout();

	return ret;
}
