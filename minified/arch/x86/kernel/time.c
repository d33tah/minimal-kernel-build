
#include <linux/clockchips.h>
#include <linux/interrupt.h>

#include <asm/time.h>

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	global_clock_event->event_handler(global_clock_event);
	return IRQ_HANDLED;
}

static void __init setup_default_timer_irq(void)
{
	if (request_irq(0, timer_interrupt, NULL))
		pr_info("Failed to register legacy timer interrupt\n");
}

void __init hpet_time_init(void)
{
	if (!pit_timer_init())
		return;

	setup_default_timer_irq();
}

/* Runs once interrupts are enabled: the PIT tick IRQ, then the TSC clocksource. */
void __init time_init(void)
{
	hpet_time_init();

	tsc_init();
}
