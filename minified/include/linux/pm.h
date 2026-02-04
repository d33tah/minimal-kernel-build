
#ifndef _LINUX_PM_H
#define _LINUX_PM_H

#include <linux/export.h>
/* linux/list.h removed - no list structures used */
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/completion.h>

/* pm_power_off removed - defined but never called */

struct device;

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

/* SYSTEM_SLEEP_PM_OPS, RUNTIME_PM_OPS, _DEFINE_DEV_PM_OPS removed - never used */

/* PM_EVENT_ON and other PMSG_*, rpm_status, DPM_FLAG_* macros removed - never used */

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
	/* subsys_data, set_latency_tolerance, qos removed - unused fields */
};


struct dev_pm_domain {
	struct dev_pm_ops	ops;
	int (*start)(struct device *dev);
	void (*detach)(struct device *dev, bool power_off);
	int (*activate)(struct device *dev);
	void (*sync)(struct device *dev);
	void (*dismiss)(struct device *dev);
};





/* device_pm_lock/unlock macros removed - were empty stubs, no callers */
/* suspend_report_result removed - unused */

#endif  
