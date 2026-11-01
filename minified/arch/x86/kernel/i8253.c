// SPDX-License-Identifier: GPL-2.0
/*
 * 8253/PIT functions
 *
 */
#include <linux/i8253.h>

/*
 * HPET replaces the PIT, when enabled. So we need to know, which of
 * the two timers is used
 */
struct clock_event_device *global_clock_event;

bool __init pit_timer_init(void)
{
	clockevent_i8253_init();
	global_clock_event = &i8253_clockevent;
	return true;
}
