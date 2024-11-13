/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_NUMA_H
#define _ASM_X86_NUMA_H

#include <linux/nodemask.h>
#include <linux/errno.h>

#include <asm/topology.h>
#include <asm/apicdef.h>

static inline void set_apicid_to_node(int apicid, s16 node)
{
}

static inline int numa_cpu_node(int cpu)
{
	return NUMA_NO_NODE;
}

# include <asm/numa_32.h>

static inline void numa_set_node(int cpu, int node)	{ }
static inline void numa_clear_node(int cpu)		{ }
static inline void init_cpu_to_node(void)		{ }
static inline void numa_add_cpu(int cpu)		{ }
static inline void numa_remove_cpu(int cpu)		{ }
static inline void init_gi_nodes(void)			{ }


static inline int numa_emu_cmdline(char *str)
{
	return -EINVAL;
}

#endif	/* _ASM_X86_NUMA_H */
