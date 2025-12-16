/* Minimal includes for PIT timer */
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/i8253.h>
#include <asm/apic.h>

struct clock_event_device *global_clock_event;

static bool __init use_pit(void)
{
	if (!IS_ENABLED(CONFIG_X86_TSC) || !boot_cpu_has(X86_FEATURE_TSC))
		return true;

	 
	return apic_needs_pit();
}

bool __init pit_timer_init(void)
{
	if (!use_pit())
		return false;

	clockevent_i8253_init(true);
	global_clock_event = &i8253_clockevent;
	return true;
}

/* init_pit_clocksource removed - not needed for minimal kernel */
