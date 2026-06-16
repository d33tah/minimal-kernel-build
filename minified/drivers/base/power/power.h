
#include <linux/pm_qos.h>

static inline void device_pm_init_common(struct device *dev)
{
	if (!dev->power.early_init) {
		spin_lock_init(&dev->power.lock);
		dev->power.qos = NULL;
		dev->power.early_init = true;
	}
}

static inline void pm_runtime_init(struct device *dev) {}

/* dpm_sysfs_add, dpm_sysfs_remove, dpm_sysfs_change_owner removed - unused */

/* device_pm_remove removed - unused */
/* device_pm_move_before, device_pm_move_after, device_pm_move_last removed - unused */

/* pm_wakeup_source_sysfs_add removed - unused */


static inline void device_pm_init(struct device *dev)
{
	device_pm_init_common(dev);
	pm_runtime_init(dev);
}
