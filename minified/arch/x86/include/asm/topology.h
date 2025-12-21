
#ifndef _ASM_X86_TOPOLOGY_H
#define _ASM_X86_TOPOLOGY_H


#include <linux/numa.h>


static inline int numa_node_id(void)
{
	return 0;
}

#define numa_node_id numa_node_id


/* Inlined from asm-generic/topology.h */
#ifndef cpu_to_node
#define cpu_to_node(cpu)	((void)(cpu),0)
#endif
#ifndef cpu_to_mem
#define cpu_to_mem(cpu)		((void)(cpu),0)
#endif
#ifndef cpumask_of_node
    #define cpumask_of_node(node)	((void)(node), cpu_online_mask)
#endif

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

struct pci_bus;
int x86_pci_root_bus_node(int bus);
void x86_pci_root_bus_resources(int bus, struct list_head *resources);

#define sysctl_sched_itmt_enabled	0

static inline void arch_scale_freq_tick(void) { }
#define arch_scale_freq_tick arch_scale_freq_tick


#endif
