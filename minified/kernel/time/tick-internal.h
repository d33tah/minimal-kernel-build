 
 
#include <linux/hrtimer.h>
#include <linux/tick.h>

#include "timekeeping.h"

struct tick_device { struct clock_event_device *evtdev; };


/* TICK_DO_TIMER_NONE removed - 0-caller object-like const */
# define TICK_DO_TIMER_BOOT	-2

DECLARE_PER_CPU(struct tick_device, tick_cpu_device);
extern ktime_t tick_next_period;
extern int tick_do_timer_cpu __read_mostly;

extern void tick_handle_periodic(struct clock_event_device *dev);
extern void tick_check_new_device(struct clock_event_device *dev);
/* tick_suspend, tick_resume removed - unused */

static inline enum clock_event_state clockevent_get_state(struct clock_event_device *dev)
{
	return dev->state_use_accessors;
}

static inline void clockevent_set_state(struct clock_event_device *dev, enum clock_event_state state)
{
	dev->state_use_accessors = state;
}

extern void clockevents_exchange_device(struct clock_event_device *old, struct clock_event_device *new);
extern void clockevents_switch_state(struct clock_event_device *dev, enum clock_event_state state);
extern void clockevents_handle_noop(struct clock_event_device *dev);


static inline void tick_set_periodic_handler(struct clock_event_device *dev, int broadcast)
{
	dev->event_handler = tick_handle_periodic;
}



/* tick_nohz_active removed: 0-caller no-HZ-off stub const */

/* hrtimer_bases DECLARE_PER_CPU removed with hrtimer.c (never read) */

/* hrtimers_resume_local removed - unused */

 
/*
 * JIFFIES_SHIFT selects the fixed-point shift for the jiffies clocksource.
 * The upstream form was:
 *   #if   HZ < 34  -> 6
 *   #elif HZ < 67  -> 7
 *   #else          -> 8
 * This build has CONFIG_HZ fixed to 250 (HZ == CONFIG_HZ == 250), so both the
 * HZ<34 and HZ<67 arms are statically FALSE and only the #else (8) is ever
 * emitted. Both dead arms dropped; the live value is inlined unconditionally.
 */
#define JIFFIES_SHIFT	8
