
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>

/* Inlined from of_irq.h */
#include <linux/irqdomain.h>
typedef int (*of_irq_init_cb_t)(struct device_node *, struct device_node *);
#define of_irq_workarounds (0)
#define of_irq_dflt_pic (NULL)
static inline int of_irq_parse_oldworld(const struct device_node *device, int index,
				      struct of_phandle_args *out_irq)
{ return -EINVAL; }
extern int of_irq_parse_raw(const __be32 *addr, struct of_phandle_args *out_irq);
extern unsigned int irq_create_of_mapping(struct of_phandle_args *irq_data);
extern int of_irq_to_resource(struct device_node *dev, int index,
			      struct resource *r);
extern void of_irq_init(const struct of_device_id *matches);
static inline int of_irq_parse_one(struct device_node *device, int index,
				   struct of_phandle_args *out_irq)
{ return -EINVAL; }
static inline int of_irq_count(struct device_node *dev)
{ return 0; }
static inline int of_irq_get(struct device_node *dev, int index)
{ return 0; }
static inline int of_irq_get_byname(struct device_node *dev, const char *name)
{ return 0; }
static inline int of_irq_to_resource_table(struct device_node *dev,
					   struct resource *res, int nr_irqs)
{ return 0; }
static inline void *of_irq_find_parent(struct device_node *child)
{ return NULL; }
static inline struct irq_domain *of_msi_get_domain(struct device *dev,
						   struct device_node *np,
						   enum irq_domain_bus_token token)
{ return NULL; }
static inline struct irq_domain *of_msi_map_get_device_domain(struct device *dev,
						u32 id, u32 bus_token)
{ return NULL; }
static inline void of_msi_configure(struct device *dev, struct device_node *np) { }
static inline u32 of_msi_map_id(struct device *dev,
				 struct device_node *msi_np, u32 id_in)
{ return id_in; }
static inline unsigned int irq_of_parse_and_map(struct device_node *dev,
						int index)
{ return 0; }
/* End of inlined of_irq.h content */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>
#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/idr.h>
#include <linux/acpi.h>
static inline int of_clk_set_defaults(struct device_node *node, bool clk_supplier) { return 0; }
#include <linux/limits.h>
#include <linux/property.h>
#include <linux/kmemleak.h>
#include <linux/types.h>
#include <linux/iommu.h>
#include <linux/dma-map-ops.h>

#include "base.h"
#include "power/power.h"

/* platform_devid_ida removed - unused */

struct device platform_bus = {
	.init_name	= "platform",
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

struct resource *platform_get_mem_or_io(struct platform_device *dev,
					unsigned int num)
{
	u32 i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		if ((resource_type(r) & (IORESOURCE_MEM|IORESOURCE_IO)) && num-- == 0)
			return r;
	}
	return NULL;
}

void __iomem *
devm_platform_get_and_ioremap_resource(struct platform_device *pdev,
				unsigned int index, struct resource **res)
{
	struct resource *r;

	r = platform_get_resource(pdev, IORESOURCE_MEM, index);
	if (res)
		*res = r;
	return devm_ioremap_resource(&pdev->dev, r);
}

void __iomem *devm_platform_ioremap_resource(struct platform_device *pdev,
					     unsigned int index)
{
	return devm_platform_get_and_ioremap_resource(pdev, index, NULL);
}

void __iomem *
devm_platform_ioremap_resource_byname(struct platform_device *pdev,
				      const char *name)
{
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	return devm_ioremap_resource(&pdev->dev, res);
}

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
		return dev_err_probe(&dev->dev, ret,
				     "IRQ index %u not found\n", num);

	return ret;
}

int devm_platform_get_irqs_affinity(struct platform_device *dev,
				    struct irq_affinity *affd,
				    unsigned int minvec,
				    unsigned int maxvec,
				    int **irqs)
{
	/* Stub: IRQ affinity not needed for minimal kernel */
	return -ENOSYS;
}

struct resource *platform_get_resource_byname(struct platform_device *dev,
					      unsigned int type,
					      const char *name)
{
	u32 i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		if (unlikely(!r->name))
			continue;

		if (type == resource_type(r) && !strcmp(r->name, name))
			return r;
	}
	return NULL;
}

struct platform_object {
	struct platform_device pdev;
	char name[];
};

static void setup_pdev_dma_masks(struct platform_device *pdev)
{
	pdev->dev.dma_parms = &pdev->dma_parms;

	if (!pdev->dev.coherent_dma_mask)
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	if (!pdev->dev.dma_mask) {
		pdev->platform_dma_mask = DMA_BIT_MASK(32);
		pdev->dev.dma_mask = &pdev->platform_dma_mask;
	}
};

static void platform_device_release(struct device *dev)
{
	struct platform_object *pa = container_of(dev, struct platform_object,
						  pdev.dev);

	of_node_put(pa->pdev.dev.of_node);
	kfree(pa->pdev.dev.platform_data);
	kfree(pa->pdev.mfd_cell);
	kfree(pa->pdev.resource);
	kfree(pa->pdev.driver_override);
	kfree(pa);
}

struct platform_device *platform_device_alloc(const char *name, int id)
{
	struct platform_object *pa;

	pa = kzalloc(sizeof(*pa) + strlen(name) + 1, GFP_KERNEL);
	if (pa) {
		strcpy(pa->name, name);
		pa->pdev.name = pa->name;
		pa->pdev.id = id;
		device_initialize(&pa->pdev.dev);
		pa->pdev.dev.release = platform_device_release;
		setup_pdev_dma_masks(&pa->pdev);
	}

	return pa ? &pa->pdev : NULL;
}

/* Stubbed: platform_device_add_resources not used externally */
int platform_device_add_resources(struct platform_device *pdev,
				  const struct resource *res, unsigned int num) { return 0; }

/* Stubbed: platform_device_add_data not used externally */
int platform_device_add_data(struct platform_device *pdev, const void *data,
			     size_t size) { return 0; }

/* Stubbed: platform_device_add not used externally */
int platform_device_add(struct platform_device *pdev) { return -ENOSYS; }

/* Stubbed: platform_device_del not used externally */
void platform_device_del(struct platform_device *pdev) { }

/* Stubbed: platform_device_register not used externally */
int platform_device_register(struct platform_device *pdev) { return -ENOSYS; }

/* Stubbed: platform_device_unregister not used externally */
void platform_device_unregister(struct platform_device *pdev) { }

/* Stubbed: platform_device_register_full not used externally */
struct platform_device *platform_device_register_full(
		const struct platform_device_info *pdevinfo) { return ERR_PTR(-ENOSYS); }

int __platform_driver_register(struct platform_driver *drv,
				struct module *owner)
{
	drv->driver.owner = owner;
	drv->driver.bus = &platform_bus_type;

	return driver_register(&drv->driver);
}

void platform_driver_unregister(struct platform_driver *drv)
{
	driver_unregister(&drv->driver);
}

/* Stubbed: __platform_driver_probe not used externally */
int __init_or_module __platform_driver_probe(struct platform_driver *drv,
		int (*probe)(struct platform_device *), struct module *module) { return -ENOSYS; }

/* Stubbed: __platform_create_bundle not used externally */
struct platform_device * __init_or_module __platform_create_bundle(
			struct platform_driver *driver,
			int (*probe)(struct platform_device *),
			struct resource *res, unsigned int n_res,
			const void *data, size_t size, struct module *module) { return ERR_PTR(-ENOSYS); }

/* Stubbed: __platform_register_drivers not used externally */
int __platform_register_drivers(struct platform_driver * const *drivers,
				unsigned int count, struct module *owner) { return 0; }

/* Stubbed: platform_unregister_drivers not used externally */
void platform_unregister_drivers(struct platform_driver * const *drivers,
				 unsigned int count) { }

static const struct platform_device_id *platform_match_id(
			const struct platform_device_id *id,
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
static const struct attribute_group platform_dev_group = { .attrs = platform_dev_attrs };
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
			dev_warn(_dev, "remove callback returned a non-zero value. This will be ignored.\n");
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

static const struct dev_pm_ops platform_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_generic_runtime_suspend, pm_generic_runtime_resume, NULL)
	USE_PLATFORM_PM_SLEEP_OPS
};

struct bus_type platform_bus_type = {
	.name		= "platform",
	.dev_groups	= platform_dev_groups,
	.match		= platform_match,
	.uevent		= platform_uevent,
	.probe		= platform_probe,
	.remove		= platform_remove,
	.shutdown	= platform_shutdown,
	.dma_configure	= platform_dma_configure,
	.dma_cleanup	= platform_dma_cleanup,
	.pm		= &platform_dev_pm_ops,
};

static inline int __platform_match(struct device *dev, const void *drv)
{
	return platform_match(dev, (struct device_driver *)drv);
}

struct device *platform_find_device_by_driver(struct device *start,
					      const struct device_driver *drv)
{
	return bus_find_device(&platform_bus_type, start, drv,
			       __platform_match);
}

void __weak __init early_platform_cleanup(void) { }

int __init platform_bus_init(void)
{
	int error;

	early_platform_cleanup();

	error = device_register(&platform_bus);
	if (error) {
		put_device(&platform_bus);
		return error;
	}
	error =  bus_register(&platform_bus_type);
	if (error)
		device_unregister(&platform_bus);
	of_platform_register_reconfig_notifier();
	return error;
}
