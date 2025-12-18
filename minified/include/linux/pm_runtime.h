
#ifndef _LINUX_PM_RUNTIME_H
#define _LINUX_PM_RUNTIME_H

#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/pm.h>

#include <linux/jiffies.h>

#define RPM_ASYNC		0x01	 
#define RPM_NOWAIT		0x02	 
#define RPM_GET_PUT		0x04	 
#define RPM_AUTO		0x08	 

#define DEFINE_RUNTIME_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn) \
	_DEFINE_DEV_PM_OPS(name, pm_runtime_force_suspend, \
			   pm_runtime_force_resume, suspend_fn, \
			   resume_fn, idle_fn)

#define EXPORT_RUNTIME_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn) \
	_EXPORT_DEV_PM_OPS(name, pm_runtime_force_suspend, pm_runtime_force_resume, \
			   suspend_fn, resume_fn, idle_fn, "", "")
#define EXPORT_GPL_RUNTIME_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn) \
	_EXPORT_DEV_PM_OPS(name, pm_runtime_force_suspend, pm_runtime_force_resume, \
			   suspend_fn, resume_fn, idle_fn, "_gpl", "")
#define EXPORT_NS_RUNTIME_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn, ns) \
	_EXPORT_DEV_PM_OPS(name, pm_runtime_force_suspend, pm_runtime_force_resume, \
			   suspend_fn, resume_fn, idle_fn, "", #ns)
#define EXPORT_NS_GPL_RUNTIME_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn, ns) \
	_EXPORT_DEV_PM_OPS(name, pm_runtime_force_suspend, pm_runtime_force_resume, \
			   suspend_fn, resume_fn, idle_fn, "_gpl", #ns)


static inline int pm_generic_runtime_suspend(struct device *dev) { return 0; }
static inline int pm_generic_runtime_resume(struct device *dev) { return 0; }

static inline int __pm_runtime_idle(struct device *dev, int rpmflags)
{
	return -ENOSYS;
}
static inline int __pm_runtime_suspend(struct device *dev, int rpmflags)
{
	return -ENOSYS;
}
static inline int __pm_runtime_resume(struct device *dev, int rpmflags)
{
	return 1;
}
static inline int __pm_runtime_set_status(struct device *dev,
					    unsigned int status) { return 0; }
static inline int pm_runtime_barrier(struct device *dev) { return 0; }
static inline void pm_runtime_enable(struct device *dev) {}
static inline void __pm_runtime_disable(struct device *dev, bool c) {}
static inline void pm_runtime_get_noresume(struct device *dev) {}
static inline void pm_runtime_put_noidle(struct device *dev) {}
static inline bool pm_runtime_suspended(struct device *dev) { return false; }
static inline bool pm_runtime_active(struct device *dev) { return true; }
static inline bool pm_runtime_status_suspended(struct device *dev) { return false; }
static inline bool pm_runtime_enabled(struct device *dev) { return false; }
/* pm_runtime_mark_last_busy, __pm_runtime_use_autosuspend,
   pm_runtime_set_autosuspend_delay, pm_runtime_autosuspend_expiration
   removed - unused */
static inline void pm_runtime_get_suppliers(struct device *dev) {}
static inline void pm_runtime_put_suppliers(struct device *dev) {}
static inline void pm_runtime_release_supplier(struct device_link *link) {}


/* Compact pm_runtime_* wrapper functions */
static inline int pm_runtime_idle(struct device *dev) { return __pm_runtime_idle(dev, 0); }
static inline int pm_runtime_suspend(struct device *dev) { return __pm_runtime_suspend(dev, 0); }
static inline int pm_runtime_resume(struct device *dev) { return __pm_runtime_resume(dev, 0); }
static inline int pm_request_idle(struct device *dev) { return __pm_runtime_idle(dev, RPM_ASYNC); }
static inline int pm_runtime_get_sync(struct device *dev) { return __pm_runtime_resume(dev, RPM_GET_PUT); }
static inline int pm_runtime_resume_and_get(struct device *dev) {
	int ret = __pm_runtime_resume(dev, RPM_GET_PUT);
	if (ret < 0) { pm_runtime_put_noidle(dev); return ret; }
	return 0;
}
static inline int pm_runtime_put(struct device *dev) { return __pm_runtime_idle(dev, RPM_GET_PUT | RPM_ASYNC); }
static inline int pm_runtime_put_sync(struct device *dev) { return __pm_runtime_idle(dev, RPM_GET_PUT); }
static inline void pm_runtime_disable(struct device *dev) { __pm_runtime_disable(dev, true); }
/* pm_runtime_set_active removed - unused */

#endif
