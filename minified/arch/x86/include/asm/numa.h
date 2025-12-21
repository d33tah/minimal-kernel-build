
#ifndef _ASM_X86_NUMA_H
#define _ASM_X86_NUMA_H

#include <linux/nodemask.h>
#include <linux/errno.h>

#include <asm/topology.h>
#include <asm/apicdef.h>

static inline void set_highmem_pages_init(void) { }
static inline void init_cpu_to_node(void)		{ }
static inline void init_gi_nodes(void)			{ }

#endif
