#ifndef _LINUX_TICK_H
#define _LINUX_TICK_H

#include <linux/clockchips.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>

static inline void tick_irq_enter(void) { }


#define tick_nohz_enabled (0)
static inline void tick_nohz_idle_stop_tick(void) { }
static inline void tick_nohz_idle_enter(void) { }
static inline void tick_nohz_idle_exit(void) { }

#endif
