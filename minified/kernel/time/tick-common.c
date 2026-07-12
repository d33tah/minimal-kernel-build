#include <linux/interrupt.h>

#include <asm/irq_regs.h>

#include "tick-internal.h"

DEFINE_PER_CPU(struct tick_device, tick_cpu_device);
ktime_t tick_next_period;

int tick_do_timer_cpu __read_mostly = TICK_DO_TIMER_BOOT;

static void tick_periodic(int cpu)
{
	if (tick_do_timer_cpu == cpu) {
		raw_spin_lock(&jiffies_lock);
		write_seqcount_begin(&jiffies_seq);

		 
		tick_next_period = ktime_add_ns(tick_next_period, TICK_NSEC);

		do_timer(1);
		write_seqcount_end(&jiffies_seq);
		raw_spin_unlock(&jiffies_lock);
		update_wall_time();
	}

	update_process_times(user_mode(get_irq_regs()));
}

void tick_handle_periodic(struct clock_event_device *dev)
{
	int cpu = smp_processor_id();

	tick_periodic(cpu);

	/*
	 * TICK_ONESHOT/NO_HZ are unset, so the tick device is never switched to
	 * oneshot state -- clockevent_state_oneshot() is always false here and the
	 * oneshot reprogramming loop was unreachable.
	 */
}

static void tick_setup_device(struct tick_device *td, struct clock_event_device *newdev, int cpu, const struct cpumask *cpumask)
{

	if (!td->evtdev) {

		if (tick_do_timer_cpu == TICK_DO_TIMER_BOOT) {
			tick_do_timer_cpu = cpu;

			tick_next_period = ktime_get();
		}
	} else {
		td->evtdev->event_handler = clockevents_handle_noop;
	}

	td->evtdev = newdev;


	/*
	 * No broadcast device on this build (tick_device_uses_broadcast always
	 * false), and TICK_ONESHOT/NO_HZ are unset, so the device always goes to
	 * PERIODIC state; the broadcast and oneshot setup branches were dead.
	 */
	tick_set_periodic_handler(newdev, 0);
	clockevents_switch_state(newdev, CLOCK_EVT_STATE_PERIODIC);
}

static bool tick_check_percpu(struct clock_event_device *curdev, struct clock_event_device *newdev, int cpu)
{
	if (!cpumask_test_cpu(cpu, newdev->cpumask))
		return false;
	if (cpumask_equal(newdev->cpumask, cpumask_of(cpu)))
		return true;
	 
	if (newdev->irq >= 0)
		return false;
	 
	if (curdev && cpumask_equal(curdev->cpumask, cpumask_of(cpu)))
		return false;
	return true;
}

static bool tick_check_preferred(struct clock_event_device *curdev, struct clock_event_device *newdev)
{
	/*
	 * No clockevent device on this build carries CLOCK_EVT_FEAT_ONESHOT
	 * (the only device, i8253, is PERIODIC-only), so neither newdev nor
	 * curdev is ever oneshot -- the oneshot preference check was dead.
	 */
	return !curdev ||
		newdev->rating > curdev->rating ||
	       !cpumask_equal(curdev->cpumask, newdev->cpumask);
}

void tick_check_new_device(struct clock_event_device *newdev)
{
	struct clock_event_device *curdev;
	struct tick_device *td;
	int cpu;

	cpu = smp_processor_id();
	td = &per_cpu(tick_cpu_device, cpu);
	curdev = td->evtdev;

	/*
	 * No broadcast device on this build, so a rejected replacement just
	 * returns (tick_install_broadcast_device was a no-op) and curdev is
	 * never a broadcast device (tick_is_broadcast_device always false).
	 */
	if (!tick_check_percpu(curdev, newdev, smp_processor_id()))
		return;
	if (!tick_check_preferred(curdev, newdev))
		return;

	clockevents_exchange_device(curdev, newdev);
	tick_setup_device(td, newdev, cpu, cpumask_of(cpu));
}



