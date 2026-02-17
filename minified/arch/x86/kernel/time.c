
#include <linux/clocksource.h>
#include <linux/time.h>

#include <asm/timer.h>
#include <asm/time.h>

void __init time_init(void)
{
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
