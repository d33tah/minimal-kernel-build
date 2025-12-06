
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

#define LATE_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	.suspend_late = pm_sleep_ptr(suspend_fn), \
	.resume_early = pm_sleep_ptr(resume_fn), \
	.freeze_late = pm_sleep_ptr(suspend_fn), \
	.thaw_early = pm_sleep_ptr(resume_fn), \
	.poweroff_late = pm_sleep_ptr(suspend_fn), \
	.restore_early = pm_sleep_ptr(resume_fn),

#define NOIRQ_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	.suspend_noirq = pm_sleep_ptr(suspend_fn), \
	.resume_noirq = pm_sleep_ptr(resume_fn), \
	.freeze_noirq = pm_sleep_ptr(suspend_fn), \
	.thaw_noirq = pm_sleep_ptr(resume_fn), \
	.poweroff_noirq = pm_sleep_ptr(suspend_fn), \
	.restore_noirq = pm_sleep_ptr(resume_fn),

#define RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn) \
	.runtime_suspend = suspend_fn, \
	.runtime_resume = resume_fn, \
	.runtime_idle = idle_fn,

#define SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)

#define SET_LATE_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)

#define SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)

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

#define DEFINE_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
	_DEFINE_DEV_PM_OPS(name, suspend_fn, resume_fn, NULL, NULL, NULL)

#define EXPORT_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
	_EXPORT_DEV_PM_OPS(name, suspend_fn, resume_fn, NULL, NULL, NULL, "", "")
#define EXPORT_GPL_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
	_EXPORT_DEV_PM_OPS(name, suspend_fn, resume_fn, NULL, NULL, NULL, "_gpl", "")
#define EXPORT_NS_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn, ns)	\
	_EXPORT_DEV_PM_OPS(name, suspend_fn, resume_fn, NULL, NULL, NULL, "", #ns)
#define EXPORT_NS_GPL_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn, ns)	\
	_EXPORT_DEV_PM_OPS(name, suspend_fn, resume_fn, NULL, NULL, NULL, "_gpl", #ns)

#define SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
const struct dev_pm_ops __maybe_unused name = { \
	SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
}

#define UNIVERSAL_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn) \
const struct dev_pm_ops __maybe_unused name = { \
	SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	SET_RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn) \
}

#define pm_ptr(_ptr) PTR_IF(IS_ENABLED(CONFIG_PM), (_ptr))
#define pm_sleep_ptr(_ptr) PTR_IF(IS_ENABLED(CONFIG_PM_SLEEP), (_ptr))


#define PM_EVENT_INVALID	(-1)
#define PM_EVENT_ON		0x0000
#define PM_EVENT_FREEZE		0x0001
#define PM_EVENT_SUSPEND	0x0002
#define PM_EVENT_RESUME		0x0010
/* Unused PM_EVENT_* removed: HIBERNATE, QUIESCE, THAW, RESTORE, RECOVER,
   USER, REMOTE, AUTO, SLEEP, SUSPEND and RESUME compound variants */

#define PMSG_INVALID	((struct pm_message){ .event = PM_EVENT_INVALID, })
#define PMSG_ON		((struct pm_message){ .event = PM_EVENT_ON, })
#define PMSG_FREEZE	((struct pm_message){ .event = PM_EVENT_FREEZE, })
#define PMSG_SUSPEND	((struct pm_message){ .event = PM_EVENT_SUSPEND, })
#define PMSG_RESUME	((struct pm_message){ .event = PM_EVENT_RESUME, })
/* Unused PMSG_* macros removed: QUIESCE, HIBERNATE, THAW, RESTORE, RECOVER,
   USER_SUSPEND, USER_RESUME, REMOTE_RESUME, AUTO_SUSPEND, AUTO_RESUME, IS_AUTO */


enum rpm_status {
	RPM_INVALID = -1,
	RPM_ACTIVE = 0,
	RPM_RESUMING,
	RPM_SUSPENDED,
	RPM_SUSPENDING,
};


/* enum rpm_request removed - unused */

struct wakeup_source;
struct wake_irq;
struct pm_domain_data;

struct pm_subsys_data {
	spinlock_t lock;
	unsigned int refcount;
};

#define DPM_FLAG_NO_DIRECT_COMPLETE	BIT(0)
#define DPM_FLAG_SMART_PREPARE		BIT(1)
#define DPM_FLAG_SMART_SUSPEND		BIT(2)
#define DPM_FLAG_MAY_SKIP_RESUME	BIT(3)

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

extern int dev_pm_get_subsys_data(struct device *dev);
extern void dev_pm_put_subsys_data(struct device *dev);

struct dev_pm_domain {
	struct dev_pm_ops	ops;
	int (*start)(struct device *dev);
	void (*detach)(struct device *dev, bool power_off);
	int (*activate)(struct device *dev);
	void (*sync)(struct device *dev);
	void (*dismiss)(struct device *dev);
};


#define PM_EVENT_PRETHAW PM_EVENT_QUIESCE



#define device_pm_lock() do {} while (0)
#define device_pm_unlock() do {} while (0)

#define suspend_report_result(dev, fn, ret)	do {} while (0)

#define pm_generic_prepare		NULL
#define pm_generic_suspend_late		NULL
#define pm_generic_suspend_noirq	NULL
#define pm_generic_suspend		NULL
#define pm_generic_resume_early		NULL
#define pm_generic_resume_noirq		NULL
#define pm_generic_resume		NULL
#define pm_generic_freeze_noirq		NULL
#define pm_generic_freeze_late		NULL
#define pm_generic_freeze		NULL
#define pm_generic_thaw_noirq		NULL
#define pm_generic_thaw_early		NULL
#define pm_generic_thaw			NULL
#define pm_generic_restore_noirq	NULL
#define pm_generic_restore_early	NULL
#define pm_generic_restore		NULL
#define pm_generic_poweroff_noirq	NULL
#define pm_generic_poweroff_late	NULL
#define pm_generic_poweroff		NULL
#define pm_generic_complete		NULL

/* Reduced dpm_order enum - only DPM_ORDER_NONE needed for stub function type */
enum dpm_order {
	DPM_ORDER_NONE,
};

#endif  
