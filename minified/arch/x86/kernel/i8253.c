/* Minimal includes for PIT timer */
#include <linux/clockchips.h>
#include <linux/i8253.h>

struct clock_event_device *global_clock_event;

bool __init pit_timer_init(void) {
	clockevent_i8253_init(true);
	global_clock_event = &i8253_clockevent;
	return true;
}

