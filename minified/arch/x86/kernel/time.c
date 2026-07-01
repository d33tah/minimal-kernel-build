
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/x86_init.h>
#include <asm/i8259.h>
#include <asm/timer.h>
#include <asm/time.h>

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	global_clock_event->event_handler(global_clock_event);
	return IRQ_HANDLED;
}

static void __init setup_default_timer_irq(void)
{
	unsigned long flags = IRQF_NOBALANCING | IRQF_IRQPOLL | IRQF_TIMER;

	 
	if (request_irq(0, timer_interrupt, flags, "timer", NULL))
		pr_info("Failed to register legacy timer interrupt\n");
}

void __init hpet_time_init(void)
{
	if (!pit_timer_init())
		return;

	setup_default_timer_irq();
}

static __init void x86_late_time_init(void)
{
	x86_init.timers.timer_init();

	tsc_init();

	if (static_cpu_has(X86_FEATURE_WAITPKG))
		use_tpause_delay();
}

void __init time_init(void)
{
	late_time_init = x86_late_time_init;
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
