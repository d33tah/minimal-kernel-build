#include <linux/cpumask.h>

#include "timekeeping.h"

/* tick_device struct, enum tick_device_mode, TICK_DO_TIMER_BOOT,
   tick_cpu_device, tick_next_period, tick_do_timer_cpu,
   tick_check_new_device, clockevent_get_state, clockevent_set_state,
   clockevents_exchange_device, clockevents_switch_state,
   clockevents_program_event all removed - entire clockevents/tick
   subsystem is dead code */

#if HZ < 34
#define JIFFIES_SHIFT	6
#elif HZ < 67
#define JIFFIES_SHIFT	7
#else
#define JIFFIES_SHIFT	8
#endif
