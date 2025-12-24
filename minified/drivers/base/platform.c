
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

static inline int dev_pm_domain_attach(struct device *dev, bool power_on)
{
	return 0;
}
static inline void dev_pm_domain_detach(struct device *dev, bool power_off)
{
}
#include <linux/idr.h>
#include <linux/acpi.h>
static inline int of_clk_set_defaults(struct device_node *node,
				      bool clk_supplier)
{
	return 0;
}
#include <linux/limits.h>
#include <linux/kmemleak.h>
#include <linux/types.h>
#include <linux/dma-map-ops.h>

#include "base.h"
#include "power/power.h"

struct device platform_bus = {
	.init_name = "platform",
};

struct resource *platform_get_resource(struct platform_device *dev,
				       unsigned int type, unsigned int num)
{
	u32 i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		if (type == resource_type(r) && num-- == 0)
			return r;
	}
	return NULL;
}

/* platform_get_mem_or_io, devm_platform_get_and_ioremap_resource,
   devm_platform_ioremap_resource, devm_platform_ioremap_resource_byname removed - unused */

/* Stub: platform_get_irq_optional not used externally */
int platform_get_irq_optional(struct platform_device *dev, unsigned int num)
{
	return -ENXIO;
}

int platform_get_irq(struct platform_device *dev, unsigned int num)
{
	int ret;

	ret = platform_get_irq_optional(dev, num);
	if (ret < 0)
		return dev_err_probe(&dev->dev, ret, "IRQ index %u not found\n",
				     num);

	return ret;
}

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

	if (of_driver_match_device(dev, drv))
		return 1;

	if (acpi_driver_match_device(dev, drv))
		return 1;

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
	int ret;

	ret = of_clk_set_defaults(_dev->of_node, false);
	if (ret < 0)
		return ret;

	ret = dev_pm_domain_attach(_dev, true);
	if (ret)
		goto out;

	if (drv->probe) {
		ret = drv->probe(dev);
		if (ret)
			dev_pm_domain_detach(_dev, true);
	}

out:
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
	dev_pm_domain_detach(_dev, true);
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

static void platform_dma_cleanup(struct device *dev)
{
}

static const struct dev_pm_ops platform_dev_pm_ops = { SET_RUNTIME_PM_OPS(
	pm_generic_runtime_suspend, pm_generic_runtime_resume,
	NULL) USE_PLATFORM_PM_SLEEP_OPS };

struct bus_type platform_bus_type = {
	.name = "platform",
	.dev_groups = platform_dev_groups,
	.match = platform_match,
	.uevent = platform_uevent,
	.probe = platform_probe,
	.remove = platform_remove,
	.shutdown = platform_shutdown,
	.dma_configure = platform_dma_configure,
	.dma_cleanup = platform_dma_cleanup,
	.pm = &platform_dev_pm_ops,
};

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
	of_platform_register_reconfig_notifier();
	return error;
}
