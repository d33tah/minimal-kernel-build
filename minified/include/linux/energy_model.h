/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ENERGY_MODEL_H
#define _LINUX_ENERGY_MODEL_H
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/jump_label.h>
#include <linux/kobject.h>
#include <linux/rcupdate.h>
#include <linux/sched/cpufreq.h>
#include <linux/sched/topology.h>
#include <linux/types.h>

/**
 * struct em_perf_state - Performance state of a performance domain
 * @frequency:	The frequency in KHz, for consistency with CPUFreq
 * @power:	The power consumed at this level (by 1 CPU or by a registered
 *		device). It can be a total power: static and dynamic.
 * @cost:	The cost coefficient associated with this level, used during
 *		energy calculation. Equal to: power * max_frequency / frequency
 * @flags:	see "em_perf_state flags" description below.
 */
struct em_perf_state {
	unsigned long frequency;
	unsigned long power;
	unsigned long cost;
	unsigned long flags;
};

/*
 * em_perf_state flags:
 *
 * EM_PERF_STATE_INEFFICIENT: The performance state is inefficient. There is
 * in this em_perf_domain, another performance state with a higher frequency
 * but a lower or equal power cost. Such inefficient states are ignored when
 * using em_pd_get_efficient_*() functions.
 */
#define EM_PERF_STATE_INEFFICIENT BIT(0)

/**
 * struct em_perf_domain - Performance domain
 * @table:		List of performance states, in ascending order
 * @nr_perf_states:	Number of performance states
 * @flags:		See "em_perf_domain flags"
 * @cpus:		Cpumask covering the CPUs of the domain. It's here
 *			for performance reasons to avoid potential cache
 *			misses during energy calculations in the scheduler
 *			and simplifies allocating/freeing that memory region.
 *
 * In case of CPU device, a "performance domain" represents a group of CPUs
 * whose performance is scaled together. All CPUs of a performance domain
 * must have the same micro-architecture. Performance domains often have
 * a 1-to-1 mapping with CPUFreq policies. In case of other devices the @cpus
 * field is unused.
 */
struct em_perf_domain {
	struct em_perf_state *table;
	int nr_perf_states;
	unsigned long flags;
	unsigned long cpus[];
};

/*
 *  em_perf_domain flags:
 *
 *  EM_PERF_DOMAIN_MILLIWATTS: The power values are in milli-Watts or some
 *  other scale.
 *
 *  EM_PERF_DOMAIN_SKIP_INEFFICIENCIES: Skip inefficient states when estimating
 *  energy consumption.
 *
 *  EM_PERF_DOMAIN_ARTIFICIAL: The power values are artificial and might be
 *  created by platform missing real power information
 */
#define EM_PERF_DOMAIN_MILLIWATTS BIT(0)
#define EM_PERF_DOMAIN_SKIP_INEFFICIENCIES BIT(1)
#define EM_PERF_DOMAIN_ARTIFICIAL BIT(2)

#define em_span_cpus(em) (to_cpumask((em)->cpus))
#define em_is_artificial(em) ((em)->flags & EM_PERF_DOMAIN_ARTIFICIAL)

struct em_data_callback {};
#define EM_ADV_DATA_CB(_active_power_cb, _cost_cb) { }
#define EM_DATA_CB(_active_power_cb) { }
#define EM_SET_ACTIVE_POWER_CB(em_cb, cb) do { } while (0)

static inline
int em_dev_register_perf_domain(struct device *dev, unsigned int nr_states,
				struct em_data_callback *cb, cpumask_t *span,
				bool milliwatts)
{
	return -EINVAL;
}
static inline void em_dev_unregister_perf_domain(struct device *dev)
{
}
static inline struct em_perf_domain *em_cpu_get(int cpu)
{
	return NULL;
}
static inline struct em_perf_domain *em_pd_get(struct device *dev)
{
	return NULL;
}
static inline unsigned long em_cpu_energy(struct em_perf_domain *pd,
			unsigned long max_util, unsigned long sum_util,
			unsigned long allowed_cpu_cap)
{
	return 0;
}
static inline int em_pd_nr_perf_states(struct em_perf_domain *pd)
{
	return 0;
}

#endif
