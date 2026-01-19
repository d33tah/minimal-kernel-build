/* Minimal includes for PIT timer */
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/i8253.h>
#include <asm/apic.h>

struct clock_event_device *global_clock_event;

/* use_pit removed - always returns true (~7 LOC) */
bool __init pit_timer_init(void)
{
	clockevent_i8253_init(true);
	global_clock_event = &i8253_clockevent;
	return true;
}
