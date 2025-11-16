 
#ifndef _ASM_X86_TOPOLOGY_H
#define _ASM_X86_TOPOLOGY_H

 
#include <linux/numa.h>


static inline int numa_node_id(void)
{
	return 0;
}
 
#define numa_node_id numa_node_id

static inline int early_cpu_to_node(int cpu)
{
	return 0;
}

static inline void setup_node_to_cpumask_map(void) { }


#include <asm-generic/topology.h>

extern const struct cpumask *cpu_coregroup_mask(int cpu);
extern const struct cpumask *cpu_clustergroup_mask(int cpu);

#define topology_logical_package_id(cpu)	(cpu_data(cpu).logical_proc_id)
#define topology_physical_package_id(cpu)	(cpu_data(cpu).phys_proc_id)
#define topology_logical_die_id(cpu)		(cpu_data(cpu).logical_die_id)
#define topology_die_id(cpu)			(cpu_data(cpu).cpu_die_id)
#define topology_core_id(cpu)			(cpu_data(cpu).cpu_core_id)
#define topology_ppin(cpu)			(cpu_data(cpu).ppin)

extern unsigned int __max_die_per_package;

#define topology_max_packages()			(1)
static inline int
topology_update_package_map(unsigned int apicid, unsigned int cpu) { return 0; }
static inline int
topology_update_die_map(unsigned int dieid, unsigned int cpu) { return 0; }
static inline int topology_phys_to_logical_pkg(unsigned int pkg) { return 0; }
static inline int topology_phys_to_logical_die(unsigned int die,
		unsigned int cpu) { return 0; }
static inline int topology_max_die_per_package(void) { return 1; }
static inline int topology_max_smt_threads(void) { return 1; }
static inline bool topology_is_primary_thread(unsigned int cpu) { return true; }
static inline bool topology_smt_supported(void) { return false; }

static inline void arch_fix_phys_package_id(int num, u32 slot)
{
}

struct pci_bus;
int x86_pci_root_bus_node(int bus);
void x86_pci_root_bus_resources(int bus, struct list_head *resources);

extern bool x86_topology_update;


#define sysctl_sched_itmt_enabled	0
static inline void sched_set_itmt_core_prio(int prio, int core_cpu)
{
}
static inline int sched_set_itmt_support(void)
{
	return 0;
}
static inline void sched_clear_itmt_support(void)
{
}

static inline void arch_set_max_freq_ratio(bool turbo_disabled) { }
static inline void freq_invariance_set_perf_ratio(u64 ratio, bool turbo_disabled) { }

extern void arch_scale_freq_tick(void);
#define arch_scale_freq_tick arch_scale_freq_tick


#endif  
