// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (c) 1991,1992,1995  Linus Torvalds
 *  Copyright (c) 1994  Alan Modra
 *  Copyright (c) 1995  Markus Kuhn
 *  Copyright (c) 1996  Ingo Molnar
 *  Copyright (c) 1998  Andrea Arcangeli
 *  Copyright (c) 2002,2006  Vojtech Pavlik
 *  Copyright (c) 2003  Andi Kleen
 *
 */

#include <linux/clockchips.h>
#include <linux/interrupt.h>

#include <asm/time.h>
#include <asm/tsc.h>

/*
 * Default timer interrupt handler for PIT/HPET
 */
static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	global_clock_event->event_handler(global_clock_event);
	return IRQ_HANDLED;
}

static void __init setup_default_timer_irq(void)
{
	if (request_irq(0, timer_interrupt, NULL))
		panic("cannot register the timer interrupt");
}

static void __init hpet_time_init(void)
{
	if (!pit_timer_init())
		return;

	setup_default_timer_irq();
}

/* Runs once interrupts are enabled: the PIT tick IRQ, then the TSC rate record. */
void __init time_init(void)
{
	hpet_time_init();

	tsc_init();
}
