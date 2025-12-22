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
extern void tick_handover_do_timer(void);

static inline void tick_irq_enter(void) { }


enum tick_broadcast_mode {
	TICK_BROADCAST_OFF,
	TICK_BROADCAST_ON,
	TICK_BROADCAST_FORCE,
};

enum tick_broadcast_state {
	TICK_BROADCAST_EXIT,
	TICK_BROADCAST_ENTER,
};




enum tick_dep_bits {
	TICK_DEP_BIT_CLOCK_UNSTABLE	= 3,
};

#define tick_nohz_enabled (0)
static inline int tick_nohz_tick_stopped(void) { return 0; }
static inline void tick_nohz_idle_stop_tick(void) { }
static inline void tick_nohz_idle_retain_tick(void) { }
static inline void tick_nohz_idle_restart_tick(void) { }
static inline void tick_nohz_idle_enter(void) { }
static inline void tick_nohz_idle_exit(void) { }

static inline bool tick_nohz_full_cpu(int cpu) { return false; }
static inline void tick_dep_set(enum tick_dep_bits bit) { }
static inline void tick_dep_clear(enum tick_dep_bits bit) { }
static inline void tick_nohz_task_switch(void) { }

/* tick_nohz_full_cpu always returns false, so body never executes */
static inline void tick_nohz_user_enter_prepare(void) { }

#endif
