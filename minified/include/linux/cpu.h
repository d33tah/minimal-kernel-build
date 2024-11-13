/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/cpu.h - generic cpu definition
 *
 * This is mainly for topological representation. We define the 
 * basic 'struct cpu' here, which can be embedded in per-arch 
 * definitions of processors.
 *
 * Basic handling of the devices is done in drivers/base/cpu.c
 *
 * CPUs are exported via sysfs in the devices/system/cpu
 * directory. 
 */
#ifndef _LINUX_CPU_H_
#define _LINUX_CPU_H_

#include <linux/node.h>
#include <linux/compiler.h>
#include <linux/cpumask.h>
#include <linux/cpuhotplug.h>

struct device;
struct device_node;
struct attribute_group;

struct cpu {
	int node_id;		/* The node which contains the CPU */
	int hotpluggable;	/* creates sysfs control file if hotpluggable */
	struct device dev;
};

extern void boot_cpu_init(void);
extern void boot_cpu_hotplug_init(void);
extern void cpu_init(void);
extern void trap_init(void);

extern int register_cpu(struct cpu *cpu, int num);
extern struct device *get_cpu_device(unsigned cpu);
extern bool cpu_is_hotpluggable(unsigned cpu);
extern bool arch_match_cpu_phys_id(int cpu, u64 phys_id);
extern bool arch_find_n_match_cpu_physical_id(struct device_node *cpun,
					      int cpu, unsigned int *thread);

extern int cpu_add_dev_attr(struct device_attribute *attr);
extern void cpu_remove_dev_attr(struct device_attribute *attr);

extern int cpu_add_dev_attr_group(struct attribute_group *attrs);
extern void cpu_remove_dev_attr_group(struct attribute_group *attrs);

extern ssize_t cpu_show_meltdown(struct device *dev,
				 struct device_attribute *attr, char *buf);
extern ssize_t cpu_show_spectre_v1(struct device *dev,
				   struct device_attribute *attr, char *buf);
extern ssize_t cpu_show_spectre_v2(struct device *dev,
				   struct device_attribute *attr, char *buf);
extern ssize_t cpu_show_spec_store_bypass(struct device *dev,
					  struct device_attribute *attr, char *buf);
extern ssize_t cpu_show_l1tf(struct device *dev,
			     struct device_attribute *attr, char *buf);
extern ssize_t cpu_show_mds(struct device *dev,
			    struct device_attribute *attr, char *buf);
extern ssize_t cpu_show_tsx_async_abort(struct device *dev,
					struct device_attribute *attr,
					char *buf);
extern ssize_t cpu_show_itlb_multihit(struct device *dev,
				      struct device_attribute *attr, char *buf);
extern ssize_t cpu_show_srbds(struct device *dev, struct device_attribute *attr, char *buf);
extern ssize_t cpu_show_mmio_stale_data(struct device *dev,
					struct device_attribute *attr,
					char *buf);
extern ssize_t cpu_show_retbleed(struct device *dev,
				 struct device_attribute *attr, char *buf);

extern __printf(4, 5)
struct device *cpu_device_create(struct device *parent, void *drvdata,
				 const struct attribute_group **groups,
				 const char *fmt, ...);

/*
 * These states are not related to the core CPU hotplug mechanism. They are
 * used by various (sub)architectures to track internal state
 */
#define CPU_ONLINE		0x0002 /* CPU is up */
#define CPU_UP_PREPARE		0x0003 /* CPU coming up */
#define CPU_DEAD		0x0007 /* CPU dead */
#define CPU_DEAD_FROZEN		0x0008 /* CPU timed out on unplug */
#define CPU_POST_DEAD		0x0009 /* CPU successfully unplugged */
#define CPU_BROKEN		0x000B /* CPU did not die properly */

#define cpuhp_tasks_frozen	0

static inline void cpu_maps_update_begin(void)
{
}

static inline void cpu_maps_update_done(void)
{
}

static inline int add_cpu(unsigned int cpu) { return 0;}

extern struct bus_type cpu_subsys;

extern int lockdep_is_cpus_held(void);


static inline void cpus_write_lock(void) { }
static inline void cpus_write_unlock(void) { }
static inline void cpus_read_lock(void) { }
static inline void cpus_read_unlock(void) { }
static inline int  cpus_read_trylock(void) { return true; }
static inline void lockdep_assert_cpus_held(void) { }
static inline void cpu_hotplug_disable(void) { }
static inline void cpu_hotplug_enable(void) { }
static inline int remove_cpu(unsigned int cpu) { return -EPERM; }
static inline void smp_shutdown_nonboot_cpus(unsigned int primary_cpu) { }

static inline void thaw_secondary_cpus(void) {}
static inline int suspend_disable_secondary_cpus(void) { return 0; }
static inline void suspend_enable_secondary_cpus(void) { }

void __noreturn cpu_startup_entry(enum cpuhp_state state);

void cpu_idle_poll_ctrl(bool enable);

/* Attach to any functions which should be considered cpuidle. */
#define __cpuidle	__section(".cpuidle.text")

bool cpu_in_idle(unsigned long pc);

void arch_cpu_idle(void);
void arch_cpu_idle_prepare(void);
void arch_cpu_idle_enter(void);
void arch_cpu_idle_exit(void);
void arch_cpu_idle_dead(void);

int cpu_report_state(int cpu);
int cpu_check_up_prepare(int cpu);
void cpu_set_state_online(int cpu);
void play_idle_precise(u64 duration_ns, u64 latency_ns);

static inline void play_idle(unsigned long duration_us)
{
	play_idle_precise(duration_us * NSEC_PER_USEC, U64_MAX);
}

static inline void cpuhp_report_idle_dead(void) { }

enum cpuhp_smt_control {
	CPU_SMT_ENABLED,
	CPU_SMT_DISABLED,
	CPU_SMT_FORCE_DISABLED,
	CPU_SMT_NOT_SUPPORTED,
	CPU_SMT_NOT_IMPLEMENTED,
};

# define cpu_smt_control		(CPU_SMT_NOT_IMPLEMENTED)
static inline void cpu_smt_disable(bool force) { }
static inline void cpu_smt_check_topology(void) { }
static inline bool cpu_smt_possible(void) { return false; }
static inline int cpuhp_smt_enable(void) { return 0; }
static inline int cpuhp_smt_disable(enum cpuhp_smt_control ctrlval) { return 0; }

extern bool cpu_mitigations_off(void);
extern bool cpu_mitigations_auto_nosmt(void);

#endif /* _LINUX_CPU_H_ */
