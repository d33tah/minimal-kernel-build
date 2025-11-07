// SPDX-License-Identifier: GPL-2.0
/*
 * List pending timers
 *
 * Copyright(C) 2006, Red Hat, Inc., Ingo Molnar
 */

#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/nmi.h>

#include <linux/uaccess.h>

#include "tick-internal.h"

struct timer_list_iter {
	int cpu;
	bool second_pass;
	u64 now;
};

/*
 * This allows printing both to /proc/timer_list and
 * to the console (on SysRq-Q):
 */
__printf(2, 3)
static void SEQ_printf(struct seq_file *m, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);

	if (m)
		seq_vprintf(m, fmt, args);
	else
		vprintk(fmt, args);

	va_end(args);
}

static void
print_timer(struct seq_file *m, struct hrtimer *taddr, struct hrtimer *timer,
	    int idx, u64 now)
{
	SEQ_printf(m, " #%d: <%pK>, %ps", idx, taddr, timer->function);
	SEQ_printf(m, ", S:%02x", timer->state);
	SEQ_printf(m, "\n");
	SEQ_printf(m, " # expires at %Lu-%Lu nsecs [in %Ld to %Ld nsecs]\n",
		(unsigned long long)ktime_to_ns(hrtimer_get_softexpires(timer)),
		(unsigned long long)ktime_to_ns(hrtimer_get_expires(timer)),
		(long long)(ktime_to_ns(hrtimer_get_softexpires(timer)) - now),
		(long long)(ktime_to_ns(hrtimer_get_expires(timer)) - now));
}

static void
print_active_timers(struct seq_file *m, struct hrtimer_clock_base *base,
		    u64 now)
{
	struct hrtimer *timer, tmp;
	unsigned long next = 0, i;
	struct timerqueue_node *curr;
	unsigned long flags;

next_one:
	i = 0;

	touch_nmi_watchdog();

	raw_spin_lock_irqsave(&base->cpu_base->lock, flags);

	curr = timerqueue_getnext(&base->active);
	/*
	 * Crude but we have to do this O(N*N) thing, because
	 * we have to unlock the base when printing:
	 */
	while (curr && i < next) {
		curr = timerqueue_iterate_next(curr);
		i++;
	}

	if (curr) {

		timer = container_of(curr, struct hrtimer, node);
		tmp = *timer;
		raw_spin_unlock_irqrestore(&base->cpu_base->lock, flags);

		print_timer(m, timer, &tmp, i, now);
		next++;
		goto next_one;
	}
	raw_spin_unlock_irqrestore(&base->cpu_base->lock, flags);
}

static void
print_base(struct seq_file *m, struct hrtimer_clock_base *base, u64 now)
{
	SEQ_printf(m, "  .base:       %pK\n", base);
	SEQ_printf(m, "  .index:      %d\n", base->index);

	SEQ_printf(m, "  .resolution: %u nsecs\n", hrtimer_resolution);

	SEQ_printf(m,   "  .get_time:   %ps\n", base->get_time);
	SEQ_printf(m,   "active timers:\n");
	print_active_timers(m, base, now + ktime_to_ns(base->offset));
}

static void print_cpu(struct seq_file *m, int cpu, u64 now)
{
	struct hrtimer_cpu_base *cpu_base = &per_cpu(hrtimer_bases, cpu);
	int i;

	SEQ_printf(m, "cpu: %d\n", cpu);
	for (i = 0; i < HRTIMER_MAX_CLOCK_BASES; i++) {
		SEQ_printf(m, " clock %d:\n", i);
		print_base(m, cpu_base->clock_base + i, now);
	}
#define P(x) \
	SEQ_printf(m, "  .%-15s: %Lu\n", #x, \
		   (unsigned long long)(cpu_base->x))
#define P_ns(x) \
	SEQ_printf(m, "  .%-15s: %Lu nsecs\n", #x, \
		   (unsigned long long)(ktime_to_ns(cpu_base->x)))

#undef P
#undef P_ns


#undef P
#undef P_ns
	SEQ_printf(m, "\n");
}

static void
print_tickdevice(struct seq_file *m, struct tick_device *td, int cpu)
{
	struct clock_event_device *dev = td->evtdev;

	touch_nmi_watchdog();

	SEQ_printf(m, "Tick Device: mode:     %d\n", td->mode);
	if (cpu < 0)
		SEQ_printf(m, "Broadcast device\n");
	else
		SEQ_printf(m, "Per CPU device: %d\n", cpu);

	SEQ_printf(m, "Clock Event Device: ");
	if (!dev) {
		SEQ_printf(m, "<NULL>\n");
		return;
	}
	SEQ_printf(m, "%s\n", dev->name);
	SEQ_printf(m, " max_delta_ns:   %llu\n",
		   (unsigned long long) dev->max_delta_ns);
	SEQ_printf(m, " min_delta_ns:   %llu\n",
		   (unsigned long long) dev->min_delta_ns);
	SEQ_printf(m, " mult:           %u\n", dev->mult);
	SEQ_printf(m, " shift:          %u\n", dev->shift);
	SEQ_printf(m, " mode:           %d\n", clockevent_get_state(dev));
	SEQ_printf(m, " next_event:     %Ld nsecs\n",
		   (unsigned long long) ktime_to_ns(dev->next_event));

	SEQ_printf(m, " set_next_event: %ps\n", dev->set_next_event);

	if (dev->set_state_shutdown)
		SEQ_printf(m, " shutdown:       %ps\n",
			dev->set_state_shutdown);

	if (dev->set_state_periodic)
		SEQ_printf(m, " periodic:       %ps\n",
			dev->set_state_periodic);

	if (dev->set_state_oneshot)
		SEQ_printf(m, " oneshot:        %ps\n",
			dev->set_state_oneshot);

	if (dev->set_state_oneshot_stopped)
		SEQ_printf(m, " oneshot stopped: %ps\n",
			dev->set_state_oneshot_stopped);

	if (dev->tick_resume)
		SEQ_printf(m, " resume:         %ps\n",
			dev->tick_resume);

	SEQ_printf(m, " event_handler:  %ps\n", dev->event_handler);
	SEQ_printf(m, "\n");
	SEQ_printf(m, " retries:        %lu\n", dev->retries);

	SEQ_printf(m, "\n");
}

static void timer_list_show_tickdevices_header(struct seq_file *m)
{
}

static inline void timer_list_header(struct seq_file *m, u64 now)
{
	SEQ_printf(m, "Timer List Version: v0.9\n");
	SEQ_printf(m, "HRTIMER_MAX_CLOCK_BASES: %d\n", HRTIMER_MAX_CLOCK_BASES);
	SEQ_printf(m, "now at %Ld nsecs\n", (unsigned long long)now);
	SEQ_printf(m, "\n");
}

void sysrq_timer_list_show(void)
{
	/* Stubbed - timer list debugging not needed */
}

