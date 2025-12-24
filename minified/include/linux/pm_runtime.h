#ifndef _LINUX_PM_RUNTIME_H
#define _LINUX_PM_RUNTIME_H
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/pm.h>
#include <linux/jiffies.h>
#define RPM_ASYNC		0x01
#define RPM_GET_PUT		0x04
static inline int pm_generic_runtime_suspend(struct device *dev) { return 0; }
static inline int pm_generic_runtime_resume(struct device *dev) { return 0; }
static inline int __pm_runtime_idle(struct device *dev, int rpmflags) { return -ENOSYS; }
static inline int __pm_runtime_resume(struct device *dev, int rpmflags) { return 1; }
static inline int pm_runtime_barrier(struct device *dev) { return 0; }
static inline void pm_runtime_put_noidle(struct device *dev) {}
static inline void pm_runtime_get_suppliers(struct device *dev) {}
static inline void pm_runtime_put_suppliers(struct device *dev) {}
static inline void pm_runtime_release_supplier(struct device_link *link) {}
static inline int pm_request_idle(struct device *dev) { return __pm_runtime_idle(dev, RPM_ASYNC); }
static inline int pm_runtime_get_sync(struct device *dev) { return __pm_runtime_resume(dev, RPM_GET_PUT); }
static inline int pm_runtime_put(struct device *dev) { return __pm_runtime_idle(dev, RPM_GET_PUT | RPM_ASYNC); }
static inline int pm_runtime_put_sync(struct device *dev) { return __pm_runtime_idle(dev, RPM_GET_PUT); }
#endif
