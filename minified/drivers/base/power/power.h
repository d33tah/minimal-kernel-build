/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/pm_qos.h>

static inline void device_pm_init_common(struct device *dev)
{
	if (!dev->power.early_init) {
		spin_lock_init(&dev->power.lock);
		dev->power.qos = NULL;
		dev->power.early_init = true;
	}
}


static inline void pm_runtime_early_init(struct device *dev)
{
	device_pm_init_common(dev);
}

static inline void pm_runtime_init(struct device *dev) {}
static inline void pm_runtime_reinit(struct device *dev) {}
static inline void pm_runtime_remove(struct device *dev) {}

static inline int dpm_sysfs_add(struct device *dev) { return 0; }
static inline void dpm_sysfs_remove(struct device *dev) {}
static inline int dpm_sysfs_change_owner(struct device *dev, kuid_t kuid,
					 kgid_t kgid) { return 0; }



static inline void device_pm_sleep_init(struct device *dev) {}

static inline void device_pm_add(struct device *dev) {}

static inline void device_pm_remove(struct device *dev)
{
	pm_runtime_remove(dev);
}

static inline void device_pm_move_before(struct device *deva,
					 struct device *devb) {}
static inline void device_pm_move_after(struct device *deva,
					struct device *devb) {}
static inline void device_pm_move_last(struct device *dev) {}

static inline void device_pm_check_callbacks(struct device *dev) {}

static inline bool device_pm_initialized(struct device *dev)
{
	return device_is_registered(dev);
}

static inline int pm_wakeup_source_sysfs_add(struct device *parent)
{
	return 0;
}


static inline void device_pm_init(struct device *dev)
{
	device_pm_init_common(dev);
	device_pm_sleep_init(dev);
	pm_runtime_init(dev);
}
