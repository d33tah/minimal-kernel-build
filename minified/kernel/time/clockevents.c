
#include <linux/clockchips.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/device.h>

#include "tick-internal.h"

static LIST_HEAD(clockevent_devices);
static LIST_HEAD(clockevents_released);
static DEFINE_RAW_SPINLOCK(clockevents_lock);
static DEFINE_MUTEX(clockevents_mutex);

static int __clockevents_switch_state(struct clock_event_device *dev,
				      enum clock_event_state state)
{
	if (dev->features & CLOCK_EVT_FEAT_DUMMY)
		return 0;

	 
	switch (state) {
	case CLOCK_EVT_STATE_DETACHED:
		 

	case CLOCK_EVT_STATE_SHUTDOWN:
		if (dev->set_state_shutdown)
			return dev->set_state_shutdown(dev);
		return 0;

	case CLOCK_EVT_STATE_PERIODIC:

		if (!(dev->features & CLOCK_EVT_FEAT_PERIODIC))
			return -ENOSYS;
		if (dev->set_state_periodic)
			return dev->set_state_periodic(dev);
		return 0;

	/*
	 * TICK_ONESHOT/HIGH_RES_TIMERS/NO_HZ are unset and broadcast is off, so
	 * the tick device is never switched to ONESHOT/ONESHOT_STOPPED -- the
	 * only clockevents_switch_state() callers pass PERIODIC/SHUTDOWN/
	 * DETACHED, so those cases were unreachable.
	 */
	default:
		return -ENOSYS;
	}
}

void clockevents_switch_state(struct clock_event_device *dev,
			      enum clock_event_state state)
{
	if (clockevent_get_state(dev) != state) {
		if (__clockevents_switch_state(dev, state))
			return;

		clockevent_set_state(dev, state);
	}
}

void clockevents_shutdown(struct clock_event_device *dev)
{
	clockevents_switch_state(dev, CLOCK_EVT_STATE_SHUTDOWN);
	dev->next_event = KTIME_MAX;
}

/*
 * TICK_ONESHOT/HIGH_RES_TIMERS/NO_HZ are all unset and broadcast is off, so the
 * tick device is only ever switched to PERIODIC state (tick_setup_periodic) and
 * its handler (tick_handle_periodic) never reprograms a next-event.  The only
 * callers of clockevents_program_event() were those dead oneshot tick paths, so
 * it and its min-delta reprogramming helpers were removed as unreachable.
 */

static void clockevents_notify_released(void)
{
	struct clock_event_device *dev;

	while (!list_empty(&clockevents_released)) {
		dev = list_entry(clockevents_released.next,
				 struct clock_event_device, list);
		list_move(&dev->list, &clockevent_devices);
		tick_check_new_device(dev);
	}
}

void clockevents_register_device(struct clock_event_device *dev)
{
	unsigned long flags;

	 
	clockevent_set_state(dev, CLOCK_EVT_STATE_DETACHED);

	if (!dev->cpumask) {
		WARN_ON(num_possible_cpus() > 1);
		dev->cpumask = cpumask_of(smp_processor_id());
	}

	if (dev->cpumask == cpu_all_mask) {
		WARN(1, "%s cpumask == cpu_all_mask, using cpu_possible_mask instead\n",
		     dev->name);
		dev->cpumask = cpu_possible_mask;
	}

	raw_spin_lock_irqsave(&clockevents_lock, flags);

	list_add(&dev->list, &clockevent_devices);
	tick_check_new_device(dev);
	clockevents_notify_released();

	raw_spin_unlock_irqrestore(&clockevents_lock, flags);
}

/*
 * The tick device is only ever switched to PERIODIC (oneshot/high-res/no-hz
 * unset, broadcast off), so set_next_event/clockevents_program_event are never
 * invoked.  The per-device mult/shift + min_delta_ns/max_delta_ns that
 * clockevents_config() computed were only read by that dead next-event
 * reprogramming path, so the freq/min_delta/max_delta arguments here are dead;
 * config_and_register just registers the device.
 */
void clockevents_config_and_register(struct clock_event_device *dev,
				     u32 freq, unsigned long min_delta,
				     unsigned long max_delta)
{
	clockevents_register_device(dev);
}

void clockevents_handle_noop(struct clock_event_device *dev)
{
}

void clockevents_exchange_device(struct clock_event_device *old,
				 struct clock_event_device *new)
{
	 
	if (old) {
		module_put(old->owner);
		clockevents_switch_state(old, CLOCK_EVT_STATE_DETACHED);
		list_move(&old->list, &clockevents_released);
	}

	if (new) {
		BUG_ON(!clockevent_state_detached(new));
		clockevents_shutdown(new);
	}
}




