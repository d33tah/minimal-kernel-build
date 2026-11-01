// SPDX-License-Identifier: GPL-2.0
/*
 * i8253 PIT clocksource
 */
#include <linux/clockchips.h>
#include <linux/i8253.h>
#include <linux/jiffies.h>
#include <linux/smp.h>

#include <asm/io.h>

static DEFINE_RAW_SPINLOCK(i8253_lock);

static int pit_set_periodic(struct clock_event_device *evt)
{
	raw_spin_lock(&i8253_lock);

	/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(0x34, PIT_MODE);
	outb_p(PIT_LATCH & 0xff, PIT_CH0); /* LSB */
	outb_p(PIT_LATCH >> 8, PIT_CH0); /* MSB */

	raw_spin_unlock(&i8253_lock);
	return 0;
}

struct clock_event_device i8253_clockevent = {
	.features = CLOCK_EVT_FEAT_PERIODIC,
	.set_state_periodic = pit_set_periodic,
};

void __init clockevent_i8253_init(void)
{
	clockevents_register_device(&i8253_clockevent);
}
