
#include <linux/pm_qos.h>

static inline void device_pm_init_common(struct device *dev)
{
	if (!dev->power.early_init) {
		spin_lock_init(&dev->power.lock);
		/* dev->power.qos removed - write-only field */
		dev->power.early_init = true;
	}
}

static inline void device_pm_init(struct device *dev)
{
	device_pm_init_common(dev);
}
