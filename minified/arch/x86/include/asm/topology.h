
#ifndef _ASM_X86_TOPOLOGY_H
#define _ASM_X86_TOPOLOGY_H

#include <linux/numa.h>

static inline int numa_node_id(void)
{
	return 0;
}

#define numa_node_id numa_node_id

#ifndef cpu_to_node
#define cpu_to_node(cpu)	((void)(cpu),0)
#endif
#ifndef cpu_to_mem
#define cpu_to_mem(cpu)		((void)(cpu),0)
#endif
#ifndef cpumask_of_node
    #define cpumask_of_node(node)	((void)(node), cpu_online_mask)
#endif

#endif
