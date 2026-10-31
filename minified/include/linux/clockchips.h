#ifndef _LINUX_CLOCKCHIPS_H
#define _LINUX_CLOCKCHIPS_H

#include <linux/cache.h>

enum clock_event_state {
	CLOCK_EVT_STATE_DETACHED,
	CLOCK_EVT_STATE_PERIODIC,
};

#define CLOCK_EVT_FEAT_PERIODIC 0x000001

struct clock_event_device {
	void (*event_handler)(struct clock_event_device *);
	enum clock_event_state state_use_accessors;
	unsigned int features;
	int (*set_state_periodic)(struct clock_event_device *);
} ____cacheline_aligned;

extern void clockevents_register_device(struct clock_event_device *dev);

#endif
