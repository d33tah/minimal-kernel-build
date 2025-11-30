 
 
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/i8253.h>

#include <asm/apic.h>
#include <asm/hpet.h>
#include <asm/time.h>
#include <asm/smp.h>

 
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

/* Stub: PIT clocksource init - not needed for minimal kernel */
static int __init init_pit_clocksource(void) { return 0; }
arch_initcall(init_pit_clocksource);
