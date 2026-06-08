
#ifndef _LINUX_PM_H
#define _LINUX_PM_H

#include <linux/export.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/completion.h>

extern void (*pm_power_off)(void);

struct device;

#define power_group_name	NULL

typedef struct pm_message {
	int event;
} pm_message_t;

struct dev_pm_ops {
	int (*prepare)(struct device *dev);
	void (*complete)(struct device *dev);
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);
	int (*freeze)(struct device *dev);
	int (*thaw)(struct device *dev);
	int (*poweroff)(struct device *dev);
	int (*restore)(struct device *dev);
	int (*suspend_late)(struct device *dev);
	int (*resume_early)(struct device *dev);
	int (*freeze_late)(struct device *dev);
	int (*thaw_early)(struct device *dev);
	int (*poweroff_late)(struct device *dev);
	int (*restore_early)(struct device *dev);
	int (*suspend_noirq)(struct device *dev);
	int (*resume_noirq)(struct device *dev);
	int (*freeze_noirq)(struct device *dev);
	int (*thaw_noirq)(struct device *dev);
	int (*poweroff_noirq)(struct device *dev);
	int (*restore_noirq)(struct device *dev);
	int (*runtime_suspend)(struct device *dev);
	int (*runtime_resume)(struct device *dev);
	int (*runtime_idle)(struct device *dev);
};

#define SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	.suspend = pm_sleep_ptr(suspend_fn), \
	.resume = pm_sleep_ptr(resume_fn), \
	.freeze = pm_sleep_ptr(suspend_fn), \
	.thaw = pm_sleep_ptr(resume_fn), \
	.poweroff = pm_sleep_ptr(suspend_fn), \
	.restore = pm_sleep_ptr(resume_fn),

#define RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn) \
	.runtime_suspend = suspend_fn, \
	.runtime_resume = resume_fn, \
	.runtime_idle = idle_fn,

#define SET_RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn)

#define _DEFINE_DEV_PM_OPS(name, \
			   suspend_fn, resume_fn, \
			   runtime_suspend_fn, runtime_resume_fn, idle_fn) \
const struct dev_pm_ops name = { \
	SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	RUNTIME_PM_OPS(runtime_suspend_fn, runtime_resume_fn, idle_fn) \
}

#define _EXPORT_DEV_PM_OPS(name, suspend_fn, resume_fn, runtime_suspend_fn, \
			   runtime_resume_fn, idle_fn, sec, ns) \
static __maybe_unused _DEFINE_DEV_PM_OPS(__static_##name, suspend_fn, \
					 resume_fn, runtime_suspend_fn, \
					 runtime_resume_fn, idle_fn)



struct wakeup_source;
struct wake_irq;
struct pm_domain_data;

struct pm_subsys_data {
	spinlock_t lock;
	unsigned int refcount;
};

struct dev_pm_info {
	pm_message_t		power_state;
	unsigned int		can_wakeup:1;
	unsigned int		async_suspend:1;
	bool			in_dpm_list:1;	 
	bool			is_prepared:1;	 
	bool			is_suspended:1;	 
	bool			is_noirq_suspended:1;
	bool			is_late_suspended:1;
	bool			no_pm:1;
	bool			early_init:1;	 
	bool			direct_complete:1;	 
	u32			driver_flags;
	spinlock_t		lock;
	unsigned int		should_wakeup:1;
	struct pm_subsys_data	*subsys_data;   
	void (*set_latency_tolerance)(struct device *, s32);
	struct dev_pm_qos	*qos;
};


struct dev_pm_domain {
	struct dev_pm_ops	ops;
	int (*start)(struct device *dev);
	void (*detach)(struct device *dev, bool power_off);
	int (*activate)(struct device *dev);
	void (*sync)(struct device *dev);
	void (*dismiss)(struct device *dev);
};





#define device_pm_lock() do {} while (0)
#define device_pm_unlock() do {} while (0)

#endif
