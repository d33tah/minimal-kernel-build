
#include <linux/clocksource.h>
#include <linux/time.h>
/* linux/export.h removed - no EXPORT_SYMBOL */

/* asm/vsyscall.h removed - empty */
/* clockchips.h, interrupt.h, irq.h, i8253.h, i8259.h removed - timer init dead */
#include <asm/timer.h>
/* hpet_enable removed - always returned 0 */
#include <asm/time.h>

/* profile_pc removed - never called */

/* timer_interrupt, hpet_time_init, x86_late_time_init removed -
   x86_init.timers.timer_init never called, entire timer init chain is dead */

void __init time_init(void)
{
	/* Stubbed for minimal Hello World - skip late time init */
}

void clocksource_arch_init(struct clocksource *cs)
{
	if (cs->vdso_clock_mode == VDSO_CLOCKMODE_NONE)
		return;

	if (cs->mask != CLOCKSOURCE_MASK(64)) {
		pr_warn("clocksource %s registered with invalid mask %016llx for VDSO. Disabling VDSO support.\n",
			cs->name, cs->mask);
		cs->vdso_clock_mode = VDSO_CLOCKMODE_NONE;
	}
}
