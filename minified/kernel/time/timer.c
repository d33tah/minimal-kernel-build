
#include <linux/interrupt.h>
#include <linux/init.h>
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

/*
 * The TIMER_SOFTIRQ machinery is removed: del_timer/schedule_timeout_uninterruptible
 * had 0 callers tree-wide (no timer is ever queued on this boot+print+stay-alive
 * artifact, the enqueue/expiry helpers were carved out in earlier passes), so the
 * timer-wheel had no expiry handler to run. run_timer_softirq was already an empty
 * no-op, run_local_timers only raised that no-op softirq, and update_process_times
 * called it -- the entire raise/handle chain was a verified behavioral no-op and
 * is dropped. update_process_times keeps the live RCU + scheduler tick work.
 */
void update_process_times(int user_tick)
{
	rcu_sched_clock_irq(user_tick);
	scheduler_tick();
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
}



