#ifndef _LINUX_SCHED_ISOLATION_H
#define _LINUX_SCHED_ISOLATION_H

#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/tick.h>

enum hk_type {
	HK_TYPE_TIMER,
	HK_TYPE_RCU,
	HK_TYPE_MISC,
	HK_TYPE_SCHED,
	HK_TYPE_TICK,
	HK_TYPE_DOMAIN,
	HK_TYPE_WQ,
	HK_TYPE_MANAGED_IRQ,
	HK_TYPE_KTHREAD,
	HK_TYPE_MAX
};


static inline const struct cpumask *housekeeping_cpumask(enum hk_type type)
{
	return cpu_possible_mask;
}

static inline void housekeeping_init(void) { }

#endif  
