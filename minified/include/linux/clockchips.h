#ifndef _LINUX_CLOCKCHIPS_H
#define _LINUX_CLOCKCHIPS_H


# include <linux/clocksource.h>
# include <linux/cpumask.h>
# include <linux/notifier.h>


enum clock_event_state { CLOCK_EVT_STATE_DETACHED, CLOCK_EVT_STATE_SHUTDOWN, CLOCK_EVT_STATE_PERIODIC, CLOCK_EVT_STATE_ONESHOT, };

# define CLOCK_EVT_FEAT_PERIODIC	0x000001

struct clock_event_device { void			(*event_handler)(struct clock_event_device *); enum clock_event_state	state_use_accessors; unsigned int		features; int			(*set_state_periodic)(struct clock_event_device *); int			(*set_state_shutdown)(struct clock_event_device *); const char		*name; int rating, irq; const struct cpumask	*cpumask; struct list_head	list; } ____cacheline_aligned;

static inline bool clockevent_state_detached(struct clock_event_device *dev) {
	return dev->state_use_accessors == CLOCK_EVT_STATE_DETACHED;
}

static inline bool clockevent_state_periodic(struct clock_event_device *dev) {
	return dev->state_use_accessors == CLOCK_EVT_STATE_PERIODIC;
}

static inline bool clockevent_state_oneshot(struct clock_event_device *dev) {
	return dev->state_use_accessors == CLOCK_EVT_STATE_ONESHOT;
}

extern void clockevents_register_device(struct clock_event_device *dev);

extern void clockevents_config_and_register(struct clock_event_device *dev, u32 freq, unsigned long min_delta, unsigned long max_delta);



#endif  
