
#ifndef _LINUX_CPUIDLE_H
#define _LINUX_CPUIDLE_H

#include <linux/percpu.h>
#include <linux/list.h>
#include <linux/hrtimer.h>

#define CPUIDLE_STATE_MAX	10
#define CPUIDLE_NAME_LEN	16
#define CPUIDLE_DESC_LEN	32

struct module;

struct cpuidle_device;
struct cpuidle_driver;



#define CPUIDLE_STATE_DISABLED_BY_USER		BIT(0)
#define CPUIDLE_STATE_DISABLED_BY_DRIVER	BIT(1)

struct cpuidle_state_usage {
	unsigned long long	disable;
	unsigned long long	usage;
	u64			time_ns;
	unsigned long long	above;  
	unsigned long long	below;  
	unsigned long long	rejected;  
};

struct cpuidle_state {
	char		name[CPUIDLE_NAME_LEN];
	char		desc[CPUIDLE_DESC_LEN];

	s64		exit_latency_ns;
	s64		target_residency_ns;
	unsigned int	flags;
	unsigned int	exit_latency;  
	int		power_usage;  
	unsigned int	target_residency;  

	int (*enter)	(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index);

	int (*enter_dead) (struct cpuidle_device *dev, int index);

	 
	int (*enter_s2idle)(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv,
			    int index);
};

#define CPUIDLE_FLAG_NONE       	(0x00)
#define CPUIDLE_FLAG_POLLING		BIT(0)  
#define CPUIDLE_FLAG_COUPLED		BIT(1)  
#define CPUIDLE_FLAG_TIMER_STOP 	BIT(2)  
#define CPUIDLE_FLAG_UNUSABLE		BIT(3)  
#define CPUIDLE_FLAG_OFF		BIT(4)  
#define CPUIDLE_FLAG_TLB_FLUSHED	BIT(5)  
#define CPUIDLE_FLAG_RCU_IDLE		BIT(6)  

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

DECLARE_PER_CPU(struct cpuidle_device *, cpuidle_devices);
DECLARE_PER_CPU(struct cpuidle_device, cpuidle_dev);


struct cpuidle_driver {
	const char		*name;
	struct module 		*owner;

         
	unsigned int            bctimer:1;
	 
	struct cpuidle_state	states[CPUIDLE_STATE_MAX];
	int			state_count;
	int			safe_state_index;

	 
	struct cpumask		*cpumask;

	 
	const char		*governor;
};

static inline void disable_cpuidle(void) { }
static inline bool cpuidle_not_available(struct cpuidle_driver *drv,
					 struct cpuidle_device *dev)
{return true; }
static inline int cpuidle_select(struct cpuidle_driver *drv,
				 struct cpuidle_device *dev, bool *stop_tick)
{return -ENODEV; }
static inline int cpuidle_enter(struct cpuidle_driver *drv,
				struct cpuidle_device *dev, int index)
{return -ENODEV; }
static inline void cpuidle_reflect(struct cpuidle_device *dev, int index) { }
static inline u64 cpuidle_poll_time(struct cpuidle_driver *drv,
			     struct cpuidle_device *dev)
{return 0; }
static inline struct cpuidle_driver *cpuidle_get_cpu_driver(
	struct cpuidle_device *dev) {return NULL; }
static inline struct cpuidle_device *cpuidle_get_device(void) {return NULL; }

static inline int cpuidle_find_deepest_state(struct cpuidle_driver *drv,
					     struct cpuidle_device *dev,
					     u64 latency_limit_ns)
{return -ENODEV; }
static inline int cpuidle_enter_s2idle(struct cpuidle_driver *drv,
				       struct cpuidle_device *dev)
{return -ENODEV; }
extern void sched_idle_set_state(struct cpuidle_state *idle_state);
extern void default_idle_call(void);

#endif  
