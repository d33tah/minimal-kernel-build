
#ifndef _PLATFORM_DEVICE_H_
#define _PLATFORM_DEVICE_H_

#include <linux/device.h>

/* struct irq_affinity, mfd_cell, property_entry removed - unused */
struct platform_device_id;

struct platform_device {
	const char	*name;
	int		id;
	bool		id_auto;
	struct device	dev;
	/* platform_dma_mask, dma_parms fields removed - never accessed */
	u32		num_resources;
	struct resource	*resource;

	const struct platform_device_id	*id_entry;
	const char *driver_override;
	/* mfd_cell, pdev_archdata fields removed - never accessed */
};

#define platform_get_device_id(pdev)	((pdev)->id_entry)

/* dev_is_platform removed - never used */
#define to_platform_device(x) container_of((x), struct platform_device, dev)

extern struct bus_type platform_bus_type;
extern struct device platform_bus;

/* platform_get_resource, platform_get_irq removed - never called */
/* platform_get_irq_optional removed - no callers */

struct platform_driver {
	int (*probe)(struct platform_device *);
	int (*remove)(struct platform_device *);
	void (*shutdown)(struct platform_device *);
	int (*suspend)(struct platform_device *, pm_message_t state);
	int (*resume)(struct platform_device *);
	struct device_driver driver;
	const struct platform_device_id *id_table;
	bool prevent_deferred_probe;
	 
	bool driver_managed_dma;
};

#define to_platform_driver(drv)	(container_of((drv), struct platform_driver, \
				 driver))

#define platform_driver_register(drv) \
	__platform_driver_register(drv, THIS_MODULE)
extern int __platform_driver_register(struct platform_driver *,
					struct module *);

/* builtin_platform_driver, platform_create_bundle, platform_register_drivers, platform_unregister_drivers removed - never called */
/* platform_pm_* macros removed - unused */

#define USE_PLATFORM_PM_SLEEP_OPS

#endif  
