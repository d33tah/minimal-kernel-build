#include <linux/clockchips.h>
#include <linux/i8253.h>
#include <linux/jiffies.h>
#include <linux/smp.h>

#include <asm/io.h>

static DEFINE_RAW_SPINLOCK(i8253_lock);

static int pit_set_periodic(struct clock_event_device *evt)
{
	raw_spin_lock(&i8253_lock);

	outb_p(0x34, PIT_MODE);
	outb_p(PIT_LATCH & 0xff, PIT_CH0);
	outb_p(PIT_LATCH >> 8, PIT_CH0);

	raw_spin_unlock(&i8253_lock);
	return 0;
}

struct clock_event_device i8253_clockevent = {
	.features = CLOCK_EVT_FEAT_PERIODIC,
	.set_state_periodic = pit_set_periodic,
};

/*
 * The tick is only ever PERIODIC here, so the oneshot path (set_state_oneshot/
 * set_next_event dispatch) is never taken; the oneshot arg is dead and the
 * device is registered PERIODIC-only.
 */
void __init clockevent_i8253_init(bool oneshot)
{
	clockevents_register_device(&i8253_clockevent);
}
