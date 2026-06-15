
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
#include <linux/sched/deadline.h>
#include <linux/sched/debug.h>
#include <linux/timer.h>
#include <linux/compat.h>

#include <linux/uaccess.h>

#include "tick-internal.h"

#define MASK_SHIFT		(HRTIMER_BASE_MONOTONIC_SOFT)
#define HRTIMER_ACTIVE_HARD	((1U << MASK_SHIFT) - 1)
#define HRTIMER_ACTIVE_SOFT	(HRTIMER_ACTIVE_HARD << MASK_SHIFT)
#define HRTIMER_ACTIVE_ALL	(HRTIMER_ACTIVE_SOFT | HRTIMER_ACTIVE_HARD)

DEFINE_PER_CPU(struct hrtimer_cpu_base, hrtimer_bases) =
{
	.lock = __RAW_SPIN_LOCK_UNLOCKED(hrtimer_bases.lock),
	.clock_base =
	{
		{
			.index = HRTIMER_BASE_MONOTONIC,
			.clockid = CLOCK_MONOTONIC,
			.get_time = &ktime_get,
		},
		{
			.index = HRTIMER_BASE_REALTIME,
			.clockid = CLOCK_REALTIME,
			.get_time = &ktime_get_real,
		},
		{
			.index = HRTIMER_BASE_BOOTTIME,
			.clockid = CLOCK_BOOTTIME,
			.get_time = &ktime_get_boottime,
		},
		{
			.index = HRTIMER_BASE_TAI,
			.clockid = CLOCK_TAI,
			.get_time = &ktime_get_clocktai,
		},
		{
			.index = HRTIMER_BASE_MONOTONIC_SOFT,
			.clockid = CLOCK_MONOTONIC,
			.get_time = &ktime_get,
		},
		{
			.index = HRTIMER_BASE_REALTIME_SOFT,
			.clockid = CLOCK_REALTIME,
			.get_time = &ktime_get_real,
		},
		{
			.index = HRTIMER_BASE_BOOTTIME_SOFT,
			.clockid = CLOCK_BOOTTIME,
			.get_time = &ktime_get_boottime,
		},
		{
			.index = HRTIMER_BASE_TAI_SOFT,
			.clockid = CLOCK_TAI,
			.get_time = &ktime_get_clocktai,
		},
	}
};

ktime_t ktime_add_safe(const ktime_t lhs, const ktime_t rhs)
{
	ktime_t res = ktime_add_unsafe(lhs, rhs);

	if (res < 0 || res < lhs || res < rhs)
		res = ktime_set(KTIME_SEC_MAX, 0);

	return res;
}


static struct hrtimer_clock_base *
__next_base(struct hrtimer_cpu_base *cpu_base, unsigned int *active)
{
	unsigned int idx;

	if (!*active)
		return NULL;

	idx = __ffs(*active);
	*active &= ~(1U << idx);

	return &cpu_base->clock_base[idx];
}

#define for_each_active_base(base, cpu_base, active)	\
	while ((base = __next_base((cpu_base), &(active))))

static ktime_t __hrtimer_next_event_base(struct hrtimer_cpu_base *cpu_base,
					 const struct hrtimer *exclude,
					 unsigned int active,
					 ktime_t expires_next)
{
	struct hrtimer_clock_base *base;
	ktime_t expires;

	for_each_active_base(base, cpu_base, active) {
		struct timerqueue_node *next;
		struct hrtimer *timer;

		next = timerqueue_getnext(&base->active);
		timer = container_of(next, struct hrtimer, node);
		if (timer == exclude) {
			
			next = timerqueue_iterate_next(next);
			if (!next)
				continue;

			timer = container_of(next, struct hrtimer, node);
		}
		expires = ktime_sub(hrtimer_get_expires(timer), base->offset);
		if (expires < expires_next) {
			expires_next = expires;

			if (exclude)
				continue;

			if (timer->is_soft)
				cpu_base->softirq_next_timer = timer;
			else
				cpu_base->next_timer = timer;
		}
	}
	
	if (expires_next < 0)
		expires_next = 0;
	return expires_next;
}

static ktime_t
__hrtimer_get_next_event(struct hrtimer_cpu_base *cpu_base, unsigned int active_mask)
{
	unsigned int active;
	struct hrtimer *next_timer = NULL;
	ktime_t expires_next = KTIME_MAX;

	if (!cpu_base->softirq_activated && (active_mask & HRTIMER_ACTIVE_SOFT)) {
		active = cpu_base->active_bases & HRTIMER_ACTIVE_SOFT;
		cpu_base->softirq_next_timer = NULL;
		expires_next = __hrtimer_next_event_base(cpu_base, NULL,
							 active, KTIME_MAX);

		next_timer = cpu_base->softirq_next_timer;
	}

	if (active_mask & HRTIMER_ACTIVE_HARD) {
		active = cpu_base->active_bases & HRTIMER_ACTIVE_HARD;
		cpu_base->next_timer = next_timer;
		expires_next = __hrtimer_next_event_base(cpu_base, NULL, active,
							 expires_next);
	}

	return expires_next;
}

static inline ktime_t hrtimer_update_base(struct hrtimer_cpu_base *base)
{
	ktime_t *offs_real = &base->clock_base[HRTIMER_BASE_REALTIME].offset;
	ktime_t *offs_boot = &base->clock_base[HRTIMER_BASE_BOOTTIME].offset;
	ktime_t *offs_tai = &base->clock_base[HRTIMER_BASE_TAI].offset;

	ktime_t now = ktime_get_update_offsets_now(&base->clock_was_set_seq,
					    offs_real, offs_boot, offs_tai);

	base->clock_base[HRTIMER_BASE_REALTIME_SOFT].offset = *offs_real;
	base->clock_base[HRTIMER_BASE_BOOTTIME_SOFT].offset = *offs_boot;
	base->clock_base[HRTIMER_BASE_TAI_SOFT].offset = *offs_tai;

	return now;
}

/*
 * No code path on this build ever enqueues an hrtimer: enqueue_hrtimer() was
 * the only writer that set cpu_base->active_bases nonzero, and it was reached
 * only from __run_hrtimer()'s restart arm, itself only reachable from the
 * (never-iterating) __hrtimer_run_queues() loop. active_bases is therefore
 * always 0, so the whole reprogram/enqueue/remove/run island was dead. The
 * softirq next-event update below short-circuits on KTIME_MAX (always, since
 * no timer is ever active), so it never needed to reprogram anything.
 */
static void
hrtimer_update_softirq_timer(struct hrtimer_cpu_base *cpu_base, bool reprogram)
{
	__hrtimer_get_next_event(cpu_base, HRTIMER_ACTIVE_SOFT);
}

static inline void
hrtimer_cpu_base_init_expiry_lock(struct hrtimer_cpu_base *base) { }
static inline void
hrtimer_cpu_base_lock_expiry(struct hrtimer_cpu_base *base) { }
static inline void
hrtimer_cpu_base_unlock_expiry(struct hrtimer_cpu_base *base) { }

static void __hrtimer_run_queues(struct hrtimer_cpu_base *cpu_base, ktime_t now,
				 unsigned long flags, unsigned int active_mask)
{
	/*
	 * cpu_base->active_bases is always 0 on this build (nothing enqueues an
	 * hrtimer), so there is never an active base to expire here.
	 */
}

static __latent_entropy void hrtimer_run_softirq(struct softirq_action *h)
{
	struct hrtimer_cpu_base *cpu_base = this_cpu_ptr(&hrtimer_bases);
	unsigned long flags;
	ktime_t now;

	hrtimer_cpu_base_lock_expiry(cpu_base);
	raw_spin_lock_irqsave(&cpu_base->lock, flags);

	now = hrtimer_update_base(cpu_base);
	__hrtimer_run_queues(cpu_base, now, flags, HRTIMER_ACTIVE_SOFT);

	cpu_base->softirq_activated = 0;
	hrtimer_update_softirq_timer(cpu_base, true);

	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);
	hrtimer_cpu_base_unlock_expiry(cpu_base);
}

void hrtimer_run_queues(void)
{
	struct hrtimer_cpu_base *cpu_base = this_cpu_ptr(&hrtimer_bases);
	unsigned long flags;
	ktime_t now;

	raw_spin_lock_irqsave(&cpu_base->lock, flags);
	now = hrtimer_update_base(cpu_base);

	if (!ktime_before(now, cpu_base->softirq_expires_next)) {
		cpu_base->softirq_expires_next = KTIME_MAX;
		cpu_base->softirq_activated = 1;
		raise_softirq_irqoff(HRTIMER_SOFTIRQ);
	}

	__hrtimer_run_queues(cpu_base, now, flags, HRTIMER_ACTIVE_HARD);
	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);
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
	cpu_base->active_bases = 0;
	cpu_base->next_timer = NULL;
	cpu_base->softirq_next_timer = NULL;
	cpu_base->expires_next = KTIME_MAX;
	cpu_base->softirq_expires_next = KTIME_MAX;
	hrtimer_cpu_base_init_expiry_lock(cpu_base);
	return 0;
}

void __init hrtimers_init(void)
{
	hrtimers_prepare_cpu(smp_processor_id());
	open_softirq(HRTIMER_SOFTIRQ, hrtimer_run_softirq);
}

