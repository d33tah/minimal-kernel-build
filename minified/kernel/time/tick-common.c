#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/module.h>

#include <asm/irq_regs.h>

#include "tick-internal.h"

DEFINE_PER_CPU(struct tick_device, tick_cpu_device);
ktime_t tick_next_period;
int tick_do_timer_cpu __read_mostly = TICK_DO_TIMER_BOOT;

/* tick_get_device removed - never called */

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
	ktime_t next = dev->next_event;

	tick_periodic(cpu);

	if (!clockevent_state_oneshot(dev))
		return;
	for (;;) {
		next = ktime_add_ns(next, TICK_NSEC);
		if (!clockevents_program_event(dev, next, false))
			return;
		if (timekeeping_valid_for_hres())
			tick_periodic(cpu);
	}
}

static void tick_setup_periodic(struct clock_event_device *dev, int broadcast)
{
	dev->event_handler = tick_handle_periodic;

	if (dev->features & CLOCK_EVT_FEAT_DUMMY)
		return;

	if (dev->features & CLOCK_EVT_FEAT_PERIODIC) {
		clockevents_switch_state(dev, CLOCK_EVT_STATE_PERIODIC);
	} else {
		unsigned int seq;
		ktime_t next;

		do {
			seq = read_seqcount_begin(&jiffies_seq);
			next = tick_next_period;
		} while (read_seqcount_retry(&jiffies_seq, seq));

		clockevents_switch_state(dev, CLOCK_EVT_STATE_ONESHOT);

		for (;;) {
			if (!clockevents_program_event(dev, next, false))
				return;
			next = ktime_add_ns(next, TICK_NSEC);
		}
	}
}

/* tick_setup_device removed - inlined into single caller (~19 LOC) */

void tick_check_new_device(struct clock_event_device *newdev)
{
	struct clock_event_device *curdev;
	struct tick_device *td;
	int cpu;

	cpu = smp_processor_id();
	td = &per_cpu(tick_cpu_device, cpu);
	curdev = td->evtdev;

	/* Simplified: just accept new device */
	if (!cpumask_test_cpu(cpu, newdev->cpumask))
		return;

	/* try_module_get always returns true - dead check removed */

	clockevents_exchange_device(curdev, newdev);

	/* Inlined tick_setup_device */
	if (!td->evtdev) {
		if (tick_do_timer_cpu == TICK_DO_TIMER_BOOT) {
			tick_do_timer_cpu = cpu;
			tick_next_period = ktime_get();
		}
		td->mode = TICKDEV_MODE_PERIODIC;
	} else {
		td->evtdev->event_handler = clockevents_handle_noop;
	}
	td->evtdev = newdev;
	/* cpumask_equal stub always returns false, irq_set_affinity is no-op */
	if (td->mode == TICKDEV_MODE_PERIODIC)
		tick_setup_periodic(newdev, 0);
}

void __init tick_init(void)
{
	/* Stubbed for minimal Hello World */
}
