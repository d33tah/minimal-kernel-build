
#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/hrtimer.h>
#include <linux/notifier.h>
#include <linux/syscalls.h>
#include <linux/interrupt.h>
#include <linux/tick.h>
#include <linux/err.h>
#include <linux/sched/signal.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/rt.h>
#include <linux/sched/debug.h>
#include <linux/timer.h>

#include <linux/uaccess.h>

#include "tick-internal.h"

/* Minimal hrtimer_cpu_base - just the lock and minimal structure */
DEFINE_PER_CPU(struct hrtimer_cpu_base, hrtimer_bases) = {
	.lock = __RAW_SPIN_LOCK_UNLOCKED(hrtimer_bases.lock),
};

ktime_t ktime_add_safe(const ktime_t lhs, const ktime_t rhs)
{
	ktime_t res = ktime_add_unsafe(lhs, rhs);
	if (res < 0 || res < lhs || res < rhs)
		res = ktime_set(KTIME_SEC_MAX, 0);
	return res;
}

void clock_was_set(unsigned int bases)
{
	/* Stubbed for minimal Hello World */
}

void clock_was_set_delayed(void)
{
	/* Stubbed for minimal Hello World */
}

void hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim, u64 delta_ns,
			    const enum hrtimer_mode mode)
{
	/* Stubbed for minimal Hello World */
}

int hrtimer_cancel(struct hrtimer *timer)
{
	/* Stubbed for minimal Hello World */
	return 0;
}

void hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
		  enum hrtimer_mode mode)
{
	/* Minimal init - just zero the structure */
	memset(timer, 0, sizeof(struct hrtimer));
	timer->base = &raw_cpu_ptr(&hrtimer_bases)->clock_base[0];
}

bool hrtimer_active(const struct hrtimer *timer)
{
	/* Stubbed for minimal Hello World */
	return false;
}

void hrtimer_run_queues(void)
{
	/* Stubbed for minimal Hello World */
}

int hrtimers_prepare_cpu(unsigned int cpu)
{
	struct hrtimer_cpu_base *cpu_base = &per_cpu(hrtimer_bases, cpu);
	int i;

	for (i = 0; i < HRTIMER_MAX_CLOCK_BASES; i++) {
		struct hrtimer_clock_base *clock_b = &cpu_base->clock_base[i];
		clock_b->cpu_base = cpu_base;
		seqcount_raw_spinlock_init(&clock_b->seq, &cpu_base->lock);
		timerqueue_init_head(&clock_b->active);
	}
	cpu_base->cpu = cpu;
	return 0;
}

void __init hrtimers_init(void)
{
	/* Minimal init for Hello World */
}
