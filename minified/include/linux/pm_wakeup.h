
#ifndef _LINUX_PM_WAKEUP_H
#define _LINUX_PM_WAKEUP_H

#ifndef _DEVICE_H_
# error "please don't include this file directly"
#endif

#include <linux/types.h>

struct wake_irq;

struct wakeup_source {
	const char 		*name;
	int			id;
	struct list_head	entry;
	spinlock_t		lock;
	struct wake_irq		*wakeirq;
	struct timer_list	timer;
	unsigned long		timer_expires;
	ktime_t total_time;
	ktime_t max_time;
	ktime_t last_time;
	ktime_t start_prevent_time;
	ktime_t prevent_sleep_time;
	unsigned long		event_count;
	unsigned long		active_count;
	unsigned long		relax_count;
	unsigned long		expire_count;
	unsigned long		wakeup_count;
	struct device		*dev;
	bool			active:1;
	bool			autosleep_enabled:1;
};

#define for_each_wakeup_source(ws) \
	for ((ws) = wakeup_sources_walk_start();	\
	     (ws);					\
	     (ws) = wakeup_sources_walk_next((ws)))


static inline void device_set_wakeup_capable(struct device *dev, bool capable)
{
	dev->power.can_wakeup = capable;
}

static inline bool device_can_wakeup(struct device *dev)
{
	return dev->power.can_wakeup;
}

static inline struct wakeup_source *wakeup_source_create(const char *name)
{
	return NULL;
}

static inline void wakeup_source_destroy(struct wakeup_source *ws) {}

static inline void wakeup_source_add(struct wakeup_source *ws) {}

static inline void wakeup_source_remove(struct wakeup_source *ws) {}

static inline struct wakeup_source *wakeup_source_register(struct device *dev,
							   const char *name)
{
	return NULL;
}

static inline void wakeup_source_unregister(struct wakeup_source *ws) {}

static inline int device_wakeup_enable(struct device *dev)
{
	dev->power.should_wakeup = true;
	return 0;
}

static inline int device_wakeup_disable(struct device *dev)
{
	dev->power.should_wakeup = false;
	return 0;
}

static inline int device_set_wakeup_enable(struct device *dev, bool enable)
{
	dev->power.should_wakeup = enable;
	return 0;
}

static inline int device_init_wakeup(struct device *dev, bool val)
{
	device_set_wakeup_capable(dev, val);
	device_set_wakeup_enable(dev, val);
	return 0;
}

static inline bool device_may_wakeup(struct device *dev)
{
	return dev->power.can_wakeup && dev->power.should_wakeup;
}

/* device_wakeup_path, device_set_wakeup_path removed - unused */
/* __pm_stay_awake, pm_stay_awake, __pm_relax, pm_relax removed - unused */
/* pm_wakeup_ws_event, pm_wakeup_dev_event removed - unused */
/* __pm_wakeup_event, pm_wakeup_event, pm_wakeup_hard_event removed - unused */

#endif  
