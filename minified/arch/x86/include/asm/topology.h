 
#ifndef _ASM_X86_TOPOLOGY_H
#define _ASM_X86_TOPOLOGY_H

 
#include <linux/numa.h>


static inline int numa_node_id(void) {
	return 0; }
 
#define numa_node_id numa_node_id

/* Inlined from asm-generic/topology.h */
#ifndef cpu_to_node
#define cpu_to_node(cpu)	((void)(cpu),0)
#endif

/* cpu_coregroup_mask, cpu_clustergroup_mask declarations removed - no implementation */

/* topology_{logical_package,physical_package,logical_die,die,core}_id, topology_ppin removed - unused (read only via these macros) */


/* x86_topology_update declaration removed - no implementation */



#endif
