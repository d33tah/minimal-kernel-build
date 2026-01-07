
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>

#include <linux/irqdomain.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>
#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include <linux/limits.h>
#include <linux/types.h>
#include <linux/dma-map-ops.h>

#include "base.h"
#include "power/power.h"

struct device platform_bus = {
	.init_name = "platform",
};

/* platform_get_resource, platform_get_irq removed - never called */

/* platform_get_mem_or_io, devm_platform_get_and_ioremap_resource,
   devm_platform_ioremap_resource, devm_platform_ioremap_resource_byname,
   platform_get_irq_optional removed - no external callers */

struct platform_object {
	struct platform_device pdev;
	char name[];
};

/* Removed: platform_device_add_resources, platform_device_add_data, platform_device_add,
   platform_device_del, platform_device_register, platform_device_unregister,
   platform_device_register_full - no external callers */

int __platform_driver_register(struct platform_driver *drv,
			       struct module *owner)
{
	drv->driver.owner = owner;
	drv->driver.bus = &platform_bus_type;

	return driver_register(&drv->driver);
}

/* Removed: __platform_driver_probe, __platform_create_bundle,
   __platform_register_drivers, platform_unregister_drivers - no external callers */

static const struct platform_device_id *
platform_match_id(const struct platform_device_id *id,
		  struct platform_device *pdev)
{
	while (id->name[0]) {
		if (strcmp(pdev->name, id->name) == 0) {
			pdev->id_entry = id;
			return id;
		}
		id++;
	}
	return NULL;
}

/* Stub: platform device sysfs attributes not needed for minimal kernel */
static struct attribute *platform_dev_attrs[] = { NULL };
static const struct attribute_group platform_dev_group = {
	.attrs = platform_dev_attrs
};
__ATTRIBUTE_GROUPS(platform_dev);

static int platform_match(struct device *dev, struct device_driver *drv)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct platform_driver *pdrv = to_platform_driver(drv);

	if (pdev->driver_override)
		return !strcmp(pdev->driver_override, drv->name);

	if (pdrv->id_table)
		return platform_match_id(pdrv->id_table, pdev) != NULL;

	return (strcmp(pdev->name, drv->name) == 0);
}

/* Stub: platform uevent simplified for minimal kernel */
static int platform_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return 0;
}

static int platform_probe(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);
	int ret = 0;

	if (drv->probe)
		ret = drv->probe(dev);

	if (drv->prevent_deferred_probe && ret == -EPROBE_DEFER) {
		dev_warn(_dev, "probe deferral not supported\n");
		ret = -ENXIO;
	}

	return ret;
}

static void platform_remove(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);

	if (drv->remove) {
		int ret = drv->remove(dev);

		if (ret)
			dev_warn(
				_dev,
				"remove callback returned a non-zero value. This will be ignored.\n");
	}
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

static const struct dev_pm_ops platform_dev_pm_ops = { SET_RUNTIME_PM_OPS(
	NULL, NULL, NULL) USE_PLATFORM_PM_SLEEP_OPS };

struct bus_type platform_bus_type = {
	.name = "platform",
	.dev_groups = platform_dev_groups,
	.match = platform_match,
	.uevent = platform_uevent,
	.probe = platform_probe,
	.remove = platform_remove,
	.shutdown = platform_shutdown,
	.dma_configure = platform_dma_configure,
	.pm = &platform_dev_pm_ops,
};

/* pbidbg debug function removed */
int __init platform_bus_init(void)
{
	int error;

	error = device_register(&platform_bus);
	if (error) {
		put_device(&platform_bus);
		return error;
	}
	error = bus_register(&platform_bus_type);
	if (error)
		device_unregister(&platform_bus);
	return error;
}
