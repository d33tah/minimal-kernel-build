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


static inline int housekeeping_any_cpu(enum hk_type type)
{
	return smp_processor_id();
}

static inline const struct cpumask *housekeeping_cpumask(enum hk_type type)
{
	return cpu_possible_mask;
}

static inline bool housekeeping_enabled(enum hk_type type)
{
	return false;
}

static inline void housekeeping_affine(struct task_struct *t,
				       enum hk_type type) { }
static inline void housekeeping_init(void) { }

static inline bool housekeeping_cpu(int cpu, enum hk_type type)
{
	return true;
}

#endif /* _LINUX_SCHED_ISOLATION_H */
