
#ifndef _PLATFORM_DEVICE_H_
#define _PLATFORM_DEVICE_H_

#include <linux/device.h>

#define PLATFORM_DEVID_NONE	(-1)
#define PLATFORM_DEVID_AUTO	(-2)

/* struct irq_affinity, mfd_cell, property_entry removed - unused */
struct platform_device_id;

struct platform_device {
	const char	*name;
	int		id;
	bool		id_auto;
	struct device	dev;
	u64		platform_dma_mask;
	struct device_dma_parameters dma_parms;
	u32		num_resources;
	struct resource	*resource;

	const struct platform_device_id	*id_entry;
	const char *driver_override;
	/* mfd_cell, pdev_archdata fields removed - never accessed */
};

#define platform_get_device_id(pdev)	((pdev)->id_entry)

#define dev_is_platform(dev) ((dev)->bus == &platform_bus_type)
#define to_platform_device(x) container_of((x), struct platform_device, dev)

extern struct bus_type platform_bus_type;
extern struct device platform_bus;

extern struct resource *platform_get_resource(struct platform_device *,
					      unsigned int, unsigned int);
extern int platform_get_irq(struct platform_device *, unsigned int);
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

#define builtin_platform_driver(__platform_driver) \
	builtin_driver(__platform_driver, platform_driver_register)

/* platform_create_bundle, platform_register_drivers, platform_unregister_drivers removed - never called */

#define platform_pm_suspend		NULL
#define platform_pm_resume		NULL

#define platform_pm_freeze		NULL
#define platform_pm_thaw		NULL
#define platform_pm_poweroff		NULL
#define platform_pm_restore		NULL

#define USE_PLATFORM_PM_SLEEP_OPS

void early_platform_cleanup(void);

#endif  
