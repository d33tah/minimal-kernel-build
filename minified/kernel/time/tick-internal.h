 
 
#include <linux/hrtimer.h>
#include <linux/tick.h>

#include "timekeeping.h"

/* --- 2025-12-08 02:15 --- Inlined from tick-sched.h */
enum tick_device_mode {
	TICKDEV_MODE_PERIODIC,
	TICKDEV_MODE_ONESHOT,
};

struct tick_device {
	struct clock_event_device *evtdev;
	enum tick_device_mode mode;
};

/* enum tick_nohz_mode removed - never used */
/* TICK_DO_TIMER_NONE removed - never used */
# define TICK_DO_TIMER_BOOT	-2

DECLARE_PER_CPU(struct tick_device, tick_cpu_device);
extern ktime_t tick_next_period;
extern int tick_do_timer_cpu __read_mostly;

/* tick_setup_periodic made static in tick-common.c */
/* tick_handle_periodic removed - never called */
extern void tick_check_new_device(struct clock_event_device *dev);
/* tick_shutdown, tick_suspend, tick_resume, tick_check_replacement, tick_install_replacement, tick_get_device removed - unused */

/* clockevents_tick_resume removed - never called */

/* tick_device_is_functional removed - inlined at single call site */

static inline enum clock_event_state clockevent_get_state(struct clock_event_device *dev)
{
	return dev->state_use_accessors;
}

static inline void clockevent_set_state(struct clock_event_device *dev,
					enum clock_event_state state)
{
	dev->state_use_accessors = state;
}

/* clockevents_shutdown made static in clockevents.c */
extern void clockevents_exchange_device(struct clock_event_device *old,
					struct clock_event_device *new);
extern void clockevents_switch_state(struct clock_event_device *dev,
				     enum clock_event_state state);
extern int clockevents_program_event(struct clock_event_device *dev,
				     ktime_t expires, bool force);
/* clockevents_handle_noop, tick_install_broadcast_device, tick_is_broadcast_device, tick_device_uses_broadcast,
 * tick_do_periodic_broadcast, tick_suspend_broadcast, tick_resume_broadcast,
 * tick_resume_check_broadcast, tick_broadcast_update_freq, tick_broadcast_init removed - never called */

 
/* tick_set_periodic_handler removed - inlined at single call site */
/* tick_setup_oneshot, tick_resume_oneshot, tick_program_event, tick_oneshot_notify, tick_oneshot_possible,
 * tick_oneshot_mode_active, tick_clock_notify, tick_check_oneshot_change,
 * tick_broadcast_switch_to_oneshot, tick_check_oneshot_broadcast_this_cpu,
 * tick_broadcast_oneshot_available, tick_broadcast_offline, tick_broadcast_oneshot_active,
 * tick_nohz_init, timers_update_nohz, tick_nohz_active removed - never called */

/* hrtimer_bases DECLARE_PER_CPU removed - never accessed */

/* get_next_timer_interrupt, timer_clear_idle removed - unused */

/* CLOCK_SET_WALL, CLOCK_SET_BOOT removed - unused */

/* clock_was_set, clock_was_set_delayed removed - never called or empty stub */
/* hrtimers_resume_local removed - unused */

 
#if HZ < 34
#define JIFFIES_SHIFT	6
#elif HZ < 67
#define JIFFIES_SHIFT	7
#else
#define JIFFIES_SHIFT	8
#endif
