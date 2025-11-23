
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
#include <linux/sched/nohz.h>
#include <linux/sched/debug.h>
#include <linux/timer.h>
#include <linux/freezer.h>
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

static const int hrtimer_clock_to_base_table[MAX_CLOCKS] = {
	
	[0 ... MAX_CLOCKS - 1]	= HRTIMER_MAX_CLOCK_BASES,

	[CLOCK_REALTIME]	= HRTIMER_BASE_REALTIME,
	[CLOCK_MONOTONIC]	= HRTIMER_BASE_MONOTONIC,
	[CLOCK_BOOTTIME]	= HRTIMER_BASE_BOOTTIME,
	[CLOCK_TAI]		= HRTIMER_BASE_TAI,
};

static inline bool is_migration_base(struct hrtimer_clock_base *base)
{
	return false;
}

static inline struct hrtimer_clock_base *
lock_hrtimer_base(const struct hrtimer *timer, unsigned long *flags)
{
	struct hrtimer_clock_base *base = timer->base;

	raw_spin_lock_irqsave(&base->cpu_base->lock, *flags);

	return base;
}

# define switch_hrtimer_base(t, b, p)	(b)

#if BITS_PER_LONG < 64

s64 __ktime_divns(const ktime_t kt, s64 div)
{
	int sft = 0;
	s64 dclc;
	u64 tmp;

	dclc = ktime_to_ns(kt);
	tmp = dclc < 0 ? -dclc : dclc;

	while (div >> 32) {
		sft++;
		div >>= 1;
	}
	tmp >>= sft;
	do_div(tmp, (u32) div);
	return dclc < 0 ? -tmp : tmp;
}
#endif 

ktime_t ktime_add_safe(const ktime_t lhs, const ktime_t rhs)
{
	ktime_t res = ktime_add_unsafe(lhs, rhs);

	if (res < 0 || res < lhs || res < rhs)
		res = ktime_set(KTIME_SEC_MAX, 0);

	return res;
}


static inline void debug_hrtimer_init(struct hrtimer *timer) { }
static inline void debug_hrtimer_activate(struct hrtimer *timer,
					  enum hrtimer_mode mode) { }
static inline void debug_hrtimer_deactivate(struct hrtimer *timer) { }

static inline void
debug_init(struct hrtimer *timer, clockid_t clockid,
	   enum hrtimer_mode mode)
{
	debug_hrtimer_init(timer);
	
}

static inline void debug_activate(struct hrtimer *timer,
				  enum hrtimer_mode mode)
{
	debug_hrtimer_activate(timer, mode);
	
}

static inline void debug_deactivate(struct hrtimer *timer)
{
	debug_hrtimer_deactivate(timer);
	
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

static ktime_t hrtimer_update_next_event(struct hrtimer_cpu_base *cpu_base)
{
	ktime_t expires_next, soft = KTIME_MAX;

	if (!cpu_base->softirq_activated) {
		soft = __hrtimer_get_next_event(cpu_base, HRTIMER_ACTIVE_SOFT);
		
		cpu_base->softirq_expires_next = soft;
	}

	expires_next = __hrtimer_get_next_event(cpu_base, HRTIMER_ACTIVE_HARD);
	
	if (expires_next > soft) {
		cpu_base->next_timer = cpu_base->softirq_next_timer;
		expires_next = soft;
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

static inline int __hrtimer_hres_active(struct hrtimer_cpu_base *cpu_base)
{
	return IS_ENABLED(CONFIG_HIGH_RES_TIMERS) ?
		cpu_base->hres_active : 0;
}

static inline int hrtimer_hres_active(void)
{
	return __hrtimer_hres_active(this_cpu_ptr(&hrtimer_bases));
}

static void __hrtimer_reprogram(struct hrtimer_cpu_base *cpu_base,
				struct hrtimer *next_timer,
				ktime_t expires_next)
{
	cpu_base->expires_next = expires_next;

	if (!__hrtimer_hres_active(cpu_base) || cpu_base->hang_detected)
		return;

	tick_program_event(expires_next, 1);
}

static void
hrtimer_force_reprogram(struct hrtimer_cpu_base *cpu_base, int skip_equal)
{
	ktime_t expires_next;

	expires_next = hrtimer_update_next_event(cpu_base);

	if (skip_equal && expires_next == cpu_base->expires_next)
		return;

	__hrtimer_reprogram(cpu_base, cpu_base->next_timer, expires_next);
}

static inline int hrtimer_is_hres_enabled(void) { return 0; }
static inline void hrtimer_switch_to_hres(void) { }

static void retrigger_next_event(void *arg)
{
	struct hrtimer_cpu_base *base = this_cpu_ptr(&hrtimer_bases);

	if (!__hrtimer_hres_active(base) && !tick_nohz_active)
		return;

	raw_spin_lock(&base->lock);
	hrtimer_update_base(base);
	if (__hrtimer_hres_active(base))
		hrtimer_force_reprogram(base, 0);
	else
		hrtimer_update_next_event(base);
	raw_spin_unlock(&base->lock);
}

static void hrtimer_reprogram(struct hrtimer *timer, bool reprogram)
{
	struct hrtimer_cpu_base *cpu_base = this_cpu_ptr(&hrtimer_bases);
	struct hrtimer_clock_base *base = timer->base;
	ktime_t expires = ktime_sub(hrtimer_get_expires(timer), base->offset);

	WARN_ON_ONCE(hrtimer_get_expires_tv64(timer) < 0);

	if (expires < 0)
		expires = 0;

	if (timer->is_soft) {
		
		struct hrtimer_cpu_base *timer_cpu_base = base->cpu_base;

		if (timer_cpu_base->softirq_activated)
			return;

		if (!ktime_before(expires, timer_cpu_base->softirq_expires_next))
			return;

		timer_cpu_base->softirq_next_timer = timer;
		timer_cpu_base->softirq_expires_next = expires;

		if (!ktime_before(expires, timer_cpu_base->expires_next) ||
		    !reprogram)
			return;
	}

	if (base->cpu_base != cpu_base)
		return;

	if (expires >= cpu_base->expires_next)
		return;

	if (cpu_base->in_hrtirq)
		return;

	cpu_base->next_timer = timer;

	__hrtimer_reprogram(cpu_base, timer, expires);
}

static bool update_needs_ipi(struct hrtimer_cpu_base *cpu_base,
			     unsigned int active)
{
	struct hrtimer_clock_base *base;
	unsigned int seq;
	ktime_t expires;

	seq = cpu_base->clock_was_set_seq;
	hrtimer_update_base(cpu_base);

	if (seq == cpu_base->clock_was_set_seq)
		return false;

	if (cpu_base->in_hrtirq)
		return false;

	active &= cpu_base->active_bases;

	for_each_active_base(base, cpu_base, active) {
		struct timerqueue_node *next;

		next = timerqueue_getnext(&base->active);
		expires = ktime_sub(next->expires, base->offset);
		if (expires < cpu_base->expires_next)
			return true;

		if (base->clockid < HRTIMER_BASE_MONOTONIC_SOFT)
			continue;
		if (cpu_base->softirq_activated)
			continue;
		if (expires < cpu_base->softirq_expires_next)
			return true;
	}
	return false;
}

void clock_was_set(unsigned int bases)
{
	struct hrtimer_cpu_base *cpu_base = raw_cpu_ptr(&hrtimer_bases);
	cpumask_var_t mask;
	int cpu;

	if (!__hrtimer_hres_active(cpu_base) && !tick_nohz_active)
		goto out_timerfd;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL)) {
		on_each_cpu(retrigger_next_event, NULL, 1);
		goto out_timerfd;
	}

	cpus_read_lock();
	for_each_online_cpu(cpu) {
		unsigned long flags;

		cpu_base = &per_cpu(hrtimer_bases, cpu);
		raw_spin_lock_irqsave(&cpu_base->lock, flags);

		if (update_needs_ipi(cpu_base, bases))
			cpumask_set_cpu(cpu, mask);

		raw_spin_unlock_irqrestore(&cpu_base->lock, flags);
	}

	preempt_disable();
	smp_call_function_many(mask, retrigger_next_event, NULL, 1);
	preempt_enable();
	cpus_read_unlock();
	free_cpumask_var(mask);

out_timerfd:
	timerfd_clock_was_set();
}

static void clock_was_set_work(struct work_struct *work)
{
	clock_was_set(CLOCK_SET_WALL);
}

static DECLARE_WORK(hrtimer_work, clock_was_set_work);

void clock_was_set_delayed(void)
{
	schedule_work(&hrtimer_work);
}

void hrtimers_resume_local(void)
{
	/* Stub: resume not needed for minimal kernel */
}

static inline
void unlock_hrtimer_base(const struct hrtimer *timer, unsigned long *flags)
{
	raw_spin_unlock_irqrestore(&timer->base->cpu_base->lock, *flags);
}

u64 hrtimer_forward(struct hrtimer *timer, ktime_t now, ktime_t interval)
{
	u64 orun = 1;
	ktime_t delta;

	delta = ktime_sub(now, hrtimer_get_expires(timer));

	if (delta < 0)
		return 0;

	if (WARN_ON(timer->state & HRTIMER_STATE_ENQUEUED))
		return 0;

	if (interval < hrtimer_resolution)
		interval = hrtimer_resolution;

	if (unlikely(delta >= interval)) {
		s64 incr = ktime_to_ns(interval);

		orun = ktime_divns(delta, incr);
		hrtimer_add_expires_ns(timer, incr * orun);
		if (hrtimer_get_expires_tv64(timer) > now)
			return orun;
		
		orun++;
	}
	hrtimer_add_expires(timer, interval);

	return orun;
}

static int enqueue_hrtimer(struct hrtimer *timer,
			   struct hrtimer_clock_base *base,
			   enum hrtimer_mode mode)
{
	debug_activate(timer, mode);

	base->cpu_base->active_bases |= 1 << base->index;

	WRITE_ONCE(timer->state, HRTIMER_STATE_ENQUEUED);

	return timerqueue_add(&base->active, &timer->node);
}

static void __remove_hrtimer(struct hrtimer *timer,
			     struct hrtimer_clock_base *base,
			     u8 newstate, int reprogram)
{
	struct hrtimer_cpu_base *cpu_base = base->cpu_base;
	u8 state = timer->state;

	WRITE_ONCE(timer->state, newstate);
	if (!(state & HRTIMER_STATE_ENQUEUED))
		return;

	if (!timerqueue_del(&base->active, &timer->node))
		cpu_base->active_bases &= ~(1 << base->index);

	if (reprogram && timer == cpu_base->next_timer)
		hrtimer_force_reprogram(cpu_base, 1);
}

static inline int
remove_hrtimer(struct hrtimer *timer, struct hrtimer_clock_base *base,
	       bool restart, bool keep_local)
{
	u8 state = timer->state;

	if (state & HRTIMER_STATE_ENQUEUED) {
		bool reprogram;

		debug_deactivate(timer);
		reprogram = base->cpu_base == this_cpu_ptr(&hrtimer_bases);

		if (!restart)
			state = HRTIMER_STATE_INACTIVE;
		else
			reprogram &= !keep_local;

		__remove_hrtimer(timer, base, state, reprogram);
		return 1;
	}
	return 0;
}

static inline ktime_t hrtimer_update_lowres(struct hrtimer *timer, ktime_t tim,
					    const enum hrtimer_mode mode)
{
	return tim;
}

static void
hrtimer_update_softirq_timer(struct hrtimer_cpu_base *cpu_base, bool reprogram)
{
	ktime_t expires;

	expires = __hrtimer_get_next_event(cpu_base, HRTIMER_ACTIVE_SOFT);

	if (expires == KTIME_MAX)
		return;

	hrtimer_reprogram(cpu_base->softirq_next_timer, reprogram);
}

static int __hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
				    u64 delta_ns, const enum hrtimer_mode mode,
				    struct hrtimer_clock_base *base)
{
	struct hrtimer_clock_base *new_base;
	bool force_local, first;

	force_local = base->cpu_base == this_cpu_ptr(&hrtimer_bases);
	force_local &= base->cpu_base->next_timer == timer;

	remove_hrtimer(timer, base, true, force_local);

	if (mode & HRTIMER_MODE_REL)
		tim = ktime_add_safe(tim, base->get_time());

	tim = hrtimer_update_lowres(timer, tim, mode);

	hrtimer_set_expires_range_ns(timer, tim, delta_ns);

	if (!force_local) {
		new_base = switch_hrtimer_base(timer, base,
					       mode & HRTIMER_MODE_PINNED);
	} else {
		new_base = base;
	}

	first = enqueue_hrtimer(timer, new_base, mode);
	if (!force_local)
		return first;

	hrtimer_force_reprogram(new_base->cpu_base, 1);
	return 0;
}

void hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
			    u64 delta_ns, const enum hrtimer_mode mode)
{
	struct hrtimer_clock_base *base;
	unsigned long flags;

	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		WARN_ON_ONCE(!(mode & HRTIMER_MODE_SOFT) ^ !timer->is_soft);
	else
		WARN_ON_ONCE(!(mode & HRTIMER_MODE_HARD) ^ !timer->is_hard);

	base = lock_hrtimer_base(timer, &flags);

	if (__hrtimer_start_range_ns(timer, tim, delta_ns, mode, base))
		hrtimer_reprogram(timer, true);

	unlock_hrtimer_base(timer, &flags);
}

int hrtimer_try_to_cancel(struct hrtimer *timer)
{
	struct hrtimer_clock_base *base;
	unsigned long flags;
	int ret = -1;

	if (!hrtimer_active(timer))
		return 0;

	base = lock_hrtimer_base(timer, &flags);

	if (!hrtimer_callback_running(timer))
		ret = remove_hrtimer(timer, base, false, false);

	unlock_hrtimer_base(timer, &flags);

	return ret;

}

static inline void
hrtimer_cpu_base_init_expiry_lock(struct hrtimer_cpu_base *base) { }
static inline void
hrtimer_cpu_base_lock_expiry(struct hrtimer_cpu_base *base) { }
static inline void
hrtimer_cpu_base_unlock_expiry(struct hrtimer_cpu_base *base) { }
static inline void hrtimer_sync_wait_running(struct hrtimer_cpu_base *base,
					     unsigned long flags) { }

int hrtimer_cancel(struct hrtimer *timer)
{
	int ret;

	do {
		ret = hrtimer_try_to_cancel(timer);

		if (ret < 0)
			hrtimer_cancel_wait_running(timer);
	} while (ret < 0);
	return ret;
}

ktime_t __hrtimer_get_remaining(const struct hrtimer *timer, bool adjust)
{
	unsigned long flags;
	ktime_t rem;

	lock_hrtimer_base(timer, &flags);
	if (IS_ENABLED(CONFIG_TIME_LOW_RES) && adjust)
		rem = hrtimer_expires_remaining_adjusted(timer);
	else
		rem = hrtimer_expires_remaining(timer);
	unlock_hrtimer_base(timer, &flags);

	return rem;
}

static inline int hrtimer_clockid_to_base(clockid_t clock_id)
{
	if (likely(clock_id < MAX_CLOCKS)) {
		int base = hrtimer_clock_to_base_table[clock_id];

		if (likely(base != HRTIMER_MAX_CLOCK_BASES))
			return base;
	}
	WARN(1, "Invalid clockid %d. Using MONOTONIC\n", clock_id);
	return HRTIMER_BASE_MONOTONIC;
}

static void __hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
			   enum hrtimer_mode mode)
{
	bool softtimer = !!(mode & HRTIMER_MODE_SOFT);
	struct hrtimer_cpu_base *cpu_base;
	int base;

	if (IS_ENABLED(CONFIG_PREEMPT_RT) && !(mode & HRTIMER_MODE_HARD))
		softtimer = true;

	memset(timer, 0, sizeof(struct hrtimer));

	cpu_base = raw_cpu_ptr(&hrtimer_bases);

	if (clock_id == CLOCK_REALTIME && mode & HRTIMER_MODE_REL)
		clock_id = CLOCK_MONOTONIC;

	base = softtimer ? HRTIMER_MAX_CLOCK_BASES / 2 : 0;
	base += hrtimer_clockid_to_base(clock_id);
	timer->is_soft = softtimer;
	timer->is_hard = !!(mode & HRTIMER_MODE_HARD);
	timer->base = &cpu_base->clock_base[base];
	timerqueue_init(&timer->node);
}

void hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
		  enum hrtimer_mode mode)
{
	debug_init(timer, clock_id, mode);
	__hrtimer_init(timer, clock_id, mode);
}

bool hrtimer_active(const struct hrtimer *timer)
{
	struct hrtimer_clock_base *base;
	unsigned int seq;

	do {
		base = READ_ONCE(timer->base);
		seq = raw_read_seqcount_begin(&base->seq);

		if (timer->state != HRTIMER_STATE_INACTIVE ||
		    base->running == timer)
			return true;

	} while (read_seqcount_retry(&base->seq, seq) ||
		 base != READ_ONCE(timer->base));

	return false;
}

static void __run_hrtimer(struct hrtimer_cpu_base *cpu_base,
			  struct hrtimer_clock_base *base,
			  struct hrtimer *timer, ktime_t *now,
			  unsigned long flags) __must_hold(&cpu_base->lock)
{
	enum hrtimer_restart (*fn)(struct hrtimer *);
	bool expires_in_hardirq;
	int restart;

	lockdep_assert_held(&cpu_base->lock);

	debug_deactivate(timer);
	base->running = timer;

	raw_write_seqcount_barrier(&base->seq);

	__remove_hrtimer(timer, base, HRTIMER_STATE_INACTIVE, 0);
	fn = timer->function;

	if (IS_ENABLED(CONFIG_TIME_LOW_RES))
		timer->is_rel = false;

	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);
	
	expires_in_hardirq = lockdep_hrtimer_enter(timer);

	restart = fn(timer);

	lockdep_hrtimer_exit(expires_in_hardirq);
	
	raw_spin_lock_irq(&cpu_base->lock);

	if (restart != HRTIMER_NORESTART &&
	    !(timer->state & HRTIMER_STATE_ENQUEUED))
		enqueue_hrtimer(timer, base, HRTIMER_MODE_ABS);

	raw_write_seqcount_barrier(&base->seq);

	WARN_ON_ONCE(base->running != timer);
	base->running = NULL;
}

static void __hrtimer_run_queues(struct hrtimer_cpu_base *cpu_base, ktime_t now,
				 unsigned long flags, unsigned int active_mask)
{
	struct hrtimer_clock_base *base;
	unsigned int active = cpu_base->active_bases & active_mask;

	for_each_active_base(base, cpu_base, active) {
		struct timerqueue_node *node;
		ktime_t basenow;

		basenow = ktime_add(now, base->offset);

		while ((node = timerqueue_getnext(&base->active))) {
			struct hrtimer *timer;

			timer = container_of(node, struct hrtimer, node);

			if (basenow < hrtimer_get_softexpires_tv64(timer))
				break;

			__run_hrtimer(cpu_base, base, timer, &basenow, flags);
			if (active_mask == HRTIMER_ACTIVE_SOFT)
				hrtimer_sync_wait_running(cpu_base, flags);
		}
	}
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

	if (__hrtimer_hres_active(cpu_base))
		return;

	if (tick_check_oneshot_change(!hrtimer_is_hres_enabled())) {
		hrtimer_switch_to_hres();
		return;
	}

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

static enum hrtimer_restart hrtimer_wakeup(struct hrtimer *timer)
{
	struct hrtimer_sleeper *t =
		container_of(timer, struct hrtimer_sleeper, timer);
	struct task_struct *task = t->task;

	t->task = NULL;
	if (task)
		wake_up_process(task);

	return HRTIMER_NORESTART;
}

void hrtimer_sleeper_start_expires(struct hrtimer_sleeper *sl,
				   enum hrtimer_mode mode)
{
	
	if (IS_ENABLED(CONFIG_PREEMPT_RT) && sl->timer.is_hard)
		mode |= HRTIMER_MODE_HARD;

	hrtimer_start_expires(&sl->timer, mode);
}

static void __hrtimer_init_sleeper(struct hrtimer_sleeper *sl,
				   clockid_t clock_id, enum hrtimer_mode mode)
{
	
	if (IS_ENABLED(CONFIG_PREEMPT_RT)) {
		if (task_is_realtime(current) && !(mode & HRTIMER_MODE_SOFT))
			mode |= HRTIMER_MODE_HARD;
	}

	__hrtimer_init(&sl->timer, clock_id, mode);
	sl->timer.function = hrtimer_wakeup;
	sl->task = current;
}

void hrtimer_init_sleeper(struct hrtimer_sleeper *sl, clockid_t clock_id,
			  enum hrtimer_mode mode)
{
	debug_init(&sl->timer, clock_id, mode);
	__hrtimer_init_sleeper(sl, clock_id, mode);

}

int nanosleep_copyout(struct restart_block *restart, struct timespec64 *ts)
{
	switch(restart->nanosleep.type) {
	case TT_NATIVE:
		if (put_timespec64(ts, restart->nanosleep.rmtp))
			return -EFAULT;
		break;
	default:
		BUG();
	}
	return -ERESTART_RESTARTBLOCK;
}

static int __sched do_nanosleep(struct hrtimer_sleeper *t, enum hrtimer_mode mode)
{
	struct restart_block *restart;

	do {
		set_current_state(TASK_INTERRUPTIBLE);
		hrtimer_sleeper_start_expires(t, mode);

		if (likely(t->task))
			freezable_schedule();

		hrtimer_cancel(&t->timer);
		mode = HRTIMER_MODE_ABS;

	} while (t->task && !signal_pending(current));

	__set_current_state(TASK_RUNNING);

	if (!t->task)
		return 0;

	restart = &current->restart_block;
	if (restart->nanosleep.type != TT_NONE) {
		ktime_t rem = hrtimer_expires_remaining(&t->timer);
		struct timespec64 rmt;

		if (rem <= 0)
			return 0;
		rmt = ktime_to_timespec64(rem);

		return nanosleep_copyout(restart, &rmt);
	}
	return -ERESTART_RESTARTBLOCK;
}

static long __sched hrtimer_nanosleep_restart(struct restart_block *restart)
{
	struct hrtimer_sleeper t;
	int ret;

	hrtimer_init_sleeper_on_stack(&t, restart->nanosleep.clockid,
				      HRTIMER_MODE_ABS);
	hrtimer_set_expires_tv64(&t.timer, restart->nanosleep.expires);
	ret = do_nanosleep(&t, HRTIMER_MODE_ABS);
	destroy_hrtimer_on_stack(&t.timer);
	return ret;
}

long hrtimer_nanosleep(ktime_t rqtp, const enum hrtimer_mode mode,
		       const clockid_t clockid)
{
	struct restart_block *restart;
	struct hrtimer_sleeper t;
	int ret = 0;
	u64 slack;

	slack = current->timer_slack_ns;
	if (dl_task(current) || rt_task(current))
		slack = 0;

	hrtimer_init_sleeper_on_stack(&t, clockid, mode);
	hrtimer_set_expires_range_ns(&t.timer, rqtp, slack);
	ret = do_nanosleep(&t, mode);
	if (ret != -ERESTART_RESTARTBLOCK)
		goto out;

	if (mode == HRTIMER_MODE_ABS) {
		ret = -ERESTARTNOHAND;
		goto out;
	}

	restart = &current->restart_block;
	restart->nanosleep.clockid = t.timer.base->clockid;
	restart->nanosleep.expires = hrtimer_get_expires_tv64(&t.timer);
	set_restart_fn(restart, hrtimer_nanosleep_restart);
out:
	destroy_hrtimer_on_stack(&t.timer);
	return ret;
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
	cpu_base->hres_active = 0;
	cpu_base->hang_detected = 0;
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

int __sched
schedule_hrtimeout_range_clock(ktime_t *expires, u64 delta,
			       const enum hrtimer_mode mode, clockid_t clock_id)
{
	struct hrtimer_sleeper t;

	if (expires && *expires == 0) {
		__set_current_state(TASK_RUNNING);
		return 0;
	}

	if (!expires) {
		schedule();
		return -EINTR;
	}

	hrtimer_init_sleeper_on_stack(&t, clock_id, mode);
	hrtimer_set_expires_range_ns(&t.timer, *expires, delta);
	hrtimer_sleeper_start_expires(&t, mode);

	if (likely(t.task))
		schedule();

	hrtimer_cancel(&t.timer);
	destroy_hrtimer_on_stack(&t.timer);

	__set_current_state(TASK_RUNNING);

	return !t.task ? 0 : -EINTR;
}

int __sched schedule_hrtimeout_range(ktime_t *expires, u64 delta,
				     const enum hrtimer_mode mode)
{
	return schedule_hrtimeout_range_clock(expires, delta, mode,
					      CLOCK_MONOTONIC);
}

int __sched schedule_hrtimeout(ktime_t *expires,
			       const enum hrtimer_mode mode)
{
	return schedule_hrtimeout_range(expires, 0, mode);
}
