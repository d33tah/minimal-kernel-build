#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>

#include <asm/irq_regs.h>

#include "timekeeping.h"

__visible u64 jiffies_64 __cacheline_aligned_in_smp = INITIAL_JIFFIES;

/*
 * Periodic tick only: there are no oneshot/NO_HZ/high-resolution timers and
 * no timer wheel (nothing in this kernel arms a timer). One CPU, so the tick
 * CPU also advances jiffies. The first clock event device
 * registered drives the tick; i8253 (PIT) is the only one in this kernel.
 */
static struct clock_event_device *tick_dev;

static void tick_handle_periodic(struct clock_event_device *dev)
{
	do_timer(1);

	rcu_sched_clock_irq(user_mode(get_irq_regs()));
	scheduler_tick();
}

static void clockevents_switch_state(struct clock_event_device *dev,
				     enum clock_event_state state)
{
	int ret = 0;

	if (dev->state_use_accessors == state)
		return;

	switch (state) {
	case CLOCK_EVT_STATE_SHUTDOWN:
		if (dev->set_state_shutdown)
			ret = dev->set_state_shutdown(dev);
		break;
	case CLOCK_EVT_STATE_PERIODIC:
		if (!(dev->features & CLOCK_EVT_FEAT_PERIODIC))
			return;
		if (dev->set_state_periodic)
			ret = dev->set_state_periodic(dev);
		break;
	default:
		return;
	}

	if (!ret)
		dev->state_use_accessors = state;
}

void clockevents_register_device(struct clock_event_device *dev)
{
	dev->state_use_accessors = CLOCK_EVT_STATE_DETACHED;

	if (tick_dev)
		return;

	tick_dev = dev;
	clockevents_switch_state(dev, CLOCK_EVT_STATE_SHUTDOWN);
	dev->event_handler = tick_handle_periodic;
	clockevents_switch_state(dev, CLOCK_EVT_STATE_PERIODIC);
}
