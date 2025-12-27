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
/* tick_handover_do_timer removed - declared but never implemented */
static inline void tick_irq_enter(void) { }
enum tick_broadcast_mode { TICK_BROADCAST_OFF, TICK_BROADCAST_ON, };
enum tick_broadcast_state { TICK_BROADCAST_EXIT, TICK_BROADCAST_ENTER, };
enum tick_dep_bits { TICK_DEP_BIT_CLOCK_UNSTABLE = 3, };
#define tick_nohz_enabled (0)
static inline void tick_nohz_idle_enter(void) { }
static inline void tick_nohz_idle_exit(void) { }
static inline void tick_nohz_task_switch(void) { }
static inline void tick_nohz_user_enter_prepare(void) { }
#endif
