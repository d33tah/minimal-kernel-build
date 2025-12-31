
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

/* topology_* macros removed - never used */

struct pci_bus;
int x86_pci_root_bus_node(int bus);
void x86_pci_root_bus_resources(int bus, struct list_head *resources);

#define sysctl_sched_itmt_enabled	0

/* arch_scale_freq_tick removed - never called */


#endif
