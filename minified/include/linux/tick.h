/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Tick related global functions
 */
#ifndef _LINUX_TICK_H
#define _LINUX_TICK_H

#include <linux/clockchips.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/context_tracking_state.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>

extern void __init tick_init(void);
/* Should be core only, but ARM BL switcher requires it */
extern void tick_suspend_local(void);
/* Should be core only, but XEN resume magic and ARM BL switcher require it */
extern void tick_resume_local(void);
extern void tick_handover_do_timer(void);
extern void tick_cleanup_dead_cpu(int cpu);

static inline void tick_freeze(void) { }
static inline void tick_unfreeze(void) { }

static inline void tick_irq_enter(void) { }

static inline void hotplug_cpu__broadcast_tick_pull(int dead_cpu) { }

enum tick_broadcast_mode {
	TICK_BROADCAST_OFF,
	TICK_BROADCAST_ON,
	TICK_BROADCAST_FORCE,
};

enum tick_broadcast_state {
	TICK_BROADCAST_EXIT,
	TICK_BROADCAST_ENTER,
};

static inline void tick_broadcast_control(enum tick_broadcast_mode mode) { }

static inline void tick_offline_cpu(unsigned int cpu) { }

extern int tick_broadcast_oneshot_control(enum tick_broadcast_state state);

static inline void tick_broadcast_enable(void)
{
	tick_broadcast_control(TICK_BROADCAST_ON);
}
static inline void tick_broadcast_disable(void)
{
	tick_broadcast_control(TICK_BROADCAST_OFF);
}
static inline void tick_broadcast_force(void)
{
	tick_broadcast_control(TICK_BROADCAST_FORCE);
}
static inline int tick_broadcast_enter(void)
{
	return tick_broadcast_oneshot_control(TICK_BROADCAST_ENTER);
}
static inline void tick_broadcast_exit(void)
{
	tick_broadcast_oneshot_control(TICK_BROADCAST_EXIT);
}

enum tick_dep_bits {
	TICK_DEP_BIT_POSIX_TIMER	= 0,
	TICK_DEP_BIT_PERF_EVENTS	= 1,
	TICK_DEP_BIT_SCHED		= 2,
	TICK_DEP_BIT_CLOCK_UNSTABLE	= 3,
	TICK_DEP_BIT_RCU		= 4,
	TICK_DEP_BIT_RCU_EXP		= 5
};
#define TICK_DEP_BIT_MAX TICK_DEP_BIT_RCU_EXP

#define TICK_DEP_MASK_NONE		0
#define TICK_DEP_MASK_POSIX_TIMER	(1 << TICK_DEP_BIT_POSIX_TIMER)
#define TICK_DEP_MASK_PERF_EVENTS	(1 << TICK_DEP_BIT_PERF_EVENTS)
#define TICK_DEP_MASK_SCHED		(1 << TICK_DEP_BIT_SCHED)
#define TICK_DEP_MASK_CLOCK_UNSTABLE	(1 << TICK_DEP_BIT_CLOCK_UNSTABLE)
#define TICK_DEP_MASK_RCU		(1 << TICK_DEP_BIT_RCU)
#define TICK_DEP_MASK_RCU_EXP		(1 << TICK_DEP_BIT_RCU_EXP)

#define tick_nohz_enabled (0)
static inline int tick_nohz_tick_stopped(void) { return 0; }
static inline int tick_nohz_tick_stopped_cpu(int cpu) { return 0; }
static inline void tick_nohz_idle_stop_tick(void) { }
static inline void tick_nohz_idle_retain_tick(void) { }
static inline void tick_nohz_idle_restart_tick(void) { }
static inline void tick_nohz_idle_enter(void) { }
static inline void tick_nohz_idle_exit(void) { }
static inline bool tick_nohz_idle_got_tick(void) { return false; }
static inline ktime_t tick_nohz_get_next_hrtimer(void)
{
	/* Next wake up is the tick period, assume it starts now */
	return ktime_add(ktime_get(), TICK_NSEC);
}
static inline ktime_t tick_nohz_get_sleep_length(ktime_t *delta_next)
{
	*delta_next = TICK_NSEC;
	return *delta_next;
}
static inline u64 get_cpu_idle_time_us(int cpu, u64 *unused) { return -1; }
static inline u64 get_cpu_iowait_time_us(int cpu, u64 *unused) { return -1; }

static inline void tick_nohz_idle_stop_tick_protected(void) { }

static inline bool tick_nohz_full_enabled(void) { return false; }
static inline bool tick_nohz_full_cpu(int cpu) { return false; }
static inline void tick_nohz_full_add_cpus_to(struct cpumask *mask) { }

static inline void tick_nohz_dep_set_cpu(int cpu, enum tick_dep_bits bit) { }
static inline void tick_nohz_dep_clear_cpu(int cpu, enum tick_dep_bits bit) { }

static inline void tick_dep_set(enum tick_dep_bits bit) { }
static inline void tick_dep_clear(enum tick_dep_bits bit) { }
static inline void tick_dep_set_cpu(int cpu, enum tick_dep_bits bit) { }
static inline void tick_dep_clear_cpu(int cpu, enum tick_dep_bits bit) { }
static inline void tick_dep_set_task(struct task_struct *tsk,
				     enum tick_dep_bits bit) { }
static inline void tick_dep_clear_task(struct task_struct *tsk,
				       enum tick_dep_bits bit) { }
static inline void tick_dep_set_signal(struct task_struct *tsk,
				       enum tick_dep_bits bit) { }
static inline void tick_dep_clear_signal(struct signal_struct *signal,
					 enum tick_dep_bits bit) { }

static inline void tick_nohz_full_kick_cpu(int cpu) { }
static inline void __tick_nohz_task_switch(void) { }
static inline void tick_nohz_full_setup(cpumask_var_t cpumask) { }

static inline void tick_nohz_task_switch(void)
{
	if (tick_nohz_full_enabled())
		__tick_nohz_task_switch();
}

static inline void tick_nohz_user_enter_prepare(void)
{
	if (tick_nohz_full_cpu(smp_processor_id()))
		rcu_nocb_flush_deferred_wakeup();
}

#endif
