#ifndef _ASM_X86_TOPOLOGY_H
#define _ASM_X86_TOPOLOGY_H

static inline int numa_node_id(void)
{
	return 0;
}

#define cpu_to_node(cpu) ((void)(cpu), 0)

#endif
