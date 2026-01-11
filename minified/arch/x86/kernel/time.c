
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i8253.h>
#include <linux/time.h>
#include <linux/export.h>

/* asm/vsyscall.h removed - empty */
#include <asm/x86_init.h>
#include <asm/i8259.h>
#include <asm/timer.h>
static inline int hpet_enable(void)
{
	return 0;
}
#include <asm/time.h>

/* profile_pc removed - never called */

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	global_clock_event->event_handler(global_clock_event);
	return IRQ_HANDLED;
}

void __init hpet_time_init(void)
{
	unsigned long flags = IRQF_NOBALANCING | IRQF_IRQPOLL | IRQF_TIMER;

#ifndef CONFIG_MMU
	/* For NOMMU, skip hardware timer init */
	return;
#endif

	if (!hpet_enable()) {
		if (!pit_timer_init())
			return;
	}

	if (request_irq(0, timer_interrupt, flags, "timer", NULL))
		pr_info("Failed to register legacy timer interrupt\n");
}

/* x86_late_time_init removed - never assigned to late_time_init pointer */

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
