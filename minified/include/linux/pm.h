
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




#define device_pm_lock() do {} while (0)
#define device_pm_unlock() do {} while (0)

#endif
