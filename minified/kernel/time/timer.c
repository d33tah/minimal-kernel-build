
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/thread_info.h>
#include <linux/jiffies.h>
#include <linux/sched/debug.h>


__visible u64 jiffies_64 __cacheline_aligned_in_smp = INITIAL_JIFFIES;



/* timer-wheel sizing: only LVL_SIZE * LVL_DEPTH (WHEEL_SIZE) survives now that
 * the timer enqueue/expiry machinery is stubbed out. */
#define LVL_BITS	6
#define LVL_SIZE	(1UL << LVL_BITS)

#if HZ > 100
# define LVL_DEPTH	9
# else
# define LVL_DEPTH	8
#endif

#define WHEEL_SIZE	(LVL_SIZE * LVL_DEPTH)

# define NR_BASES	1
# define BASE_STD	0

struct timer_base {
	raw_spinlock_t		lock;
	unsigned long		clk;
	unsigned long		next_expiry;
	bool			next_expiry_recalc;
	bool			timers_pending;
	DECLARE_BITMAP(pending_map, WHEEL_SIZE);
	struct hlist_head	vectors[WHEEL_SIZE];
} ____cacheline_aligned;

static DEFINE_PER_CPU(struct timer_base, timer_bases[NR_BASES]);

static void do_init_timer(struct timer_list *timer,
			  void (*func)(struct timer_list *),
			  unsigned int flags,
			  const char *name, struct lock_class_key *key)
{
	timer->entry.pprev = NULL;
	timer->function = func;
	if (WARN_ON_ONCE(flags & ~TIMER_INIT_FLAGS))
		flags &= TIMER_INIT_FLAGS;
	timer->flags = flags | raw_smp_processor_id();
	lockdep_init_map(&timer->lockdep_map, name, key, 0);
}

void init_timer_key(struct timer_list *timer,
		    void (*func)(struct timer_list *), unsigned int flags,
		    const char *name, struct lock_class_key *key)
{
	do_init_timer(timer, func, flags, name, key);
}

/*
 * RUNTIME-DEAD ANCHOR-STUB: del_timer (and del_timer_sync/del_singleshot_timer_sync
 * which macro-alias to it) has 0 callers tree-wide -- no timer is ever queued on
 * this artifact (schedule_timeout_uninterruptible, the only modify caller, is also
 * stubbed). Returns 0 ("timer was not pending"); never executes.
 */
int del_timer(struct timer_list *timer)
{
	return 0;
}

/*
 * RUNTIME-DEAD ANCHOR-STUB: run_timer_softirq (and its entire private subtree
 * __run_timers -> {collect_expired_timers, __next_timer_interrupt ->
 * next_pending_bucket, expire_timers -> call_timer_fn}) never executes on this
 * boot+print+stay-alive artifact (no timer ever expires before idle). The
 * TIMER_SOFTIRQ handler is link-live via open_softirq() in init_timers(), but
 * the softirq is never raised (run_local_timers's next_expiry guard never
 * fires). Stubbed to a no-op; the whole expiry machinery was carved out.
 */
static __latent_entropy void run_timer_softirq(struct softirq_action *h)
{
}

static void run_local_timers(void)
{
	struct timer_base *base = this_cpu_ptr(&timer_bases[BASE_STD]);

	if (time_before(jiffies, base->next_expiry))
		return;
	raise_softirq(TIMER_SOFTIRQ);
}

void update_process_times(int user_tick)
{
	run_local_timers();
	rcu_sched_clock_irq(user_tick);
	scheduler_tick();
}

/*
 * RUNTIME-DEAD ANCHOR-STUB: schedule_timeout_uninterruptible has 0 callers
 * tree-wide on this boot+print+stay-alive artifact (init/idle never sleeps on
 * a timer). Its whole private subtree (process_timeout, __mod_timer + the timer
 * wheel modify helpers lock_timer_base/forward_timer_base/detach_if_pending/
 * internal_add_timer/calc_wheel_index/calc_index/enqueue_timer/get_timer_base,
 * shared only with the equally-dead del_timer) was carved out. Stubbed to a
 * no-op that returns the requested timeout unchanged; it never executes.
 */
signed long __sched schedule_timeout_uninterruptible(signed long timeout)
{
	return timeout < 0 ? 0 : timeout;
}

static void __init init_timer_cpu(int cpu)
{
	struct timer_base *base;
	int i;

	for (i = 0; i < NR_BASES; i++) {
		base = per_cpu_ptr(&timer_bases[i], cpu);
		raw_spin_lock_init(&base->lock);
		base->clk = jiffies;
		base->next_expiry = base->clk + NEXT_TIMER_MAX_DELTA;
	}
}

static void __init init_timer_cpus(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		init_timer_cpu(cpu);
}

void __init init_timers(void)
{
	init_timer_cpus();
	open_softirq(TIMER_SOFTIRQ, run_timer_softirq);
}



