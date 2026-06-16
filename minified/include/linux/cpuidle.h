
#ifndef _LINUX_CPUIDLE_H
#define _LINUX_CPUIDLE_H

#include <linux/percpu.h>
#include <linux/list.h>
#include <linux/hrtimer.h>

#define CPUIDLE_STATE_MAX	10

struct module;

struct cpuidle_device;
struct cpuidle_driver;

struct cpuidle_state_usage {
	unsigned long long	disable;
	unsigned long long	usage;
	u64			time_ns;
	unsigned long long	above;
	unsigned long long	below;
	unsigned long long	rejected;
};

struct cpuidle_device_kobj;
struct cpuidle_state_kobj;
struct cpuidle_driver_kobj;

struct cpuidle_device {
	unsigned int		registered:1;
	unsigned int		enabled:1;
	unsigned int		poll_time_limit:1;
	unsigned int		cpu;
	ktime_t			next_hrtimer;

	int			last_state_idx;
	u64			last_residency_ns;
	u64			poll_limit_ns;
	u64			forced_idle_latency_limit_ns;
	struct cpuidle_state_usage	states_usage[CPUIDLE_STATE_MAX];
	struct cpuidle_state_kobj *kobjs[CPUIDLE_STATE_MAX];
	struct cpuidle_driver_kobj *kobj_driver;
	struct cpuidle_device_kobj *kobj_dev;
	struct list_head 	device_list;

};

extern void default_idle_call(void);

#endif  
