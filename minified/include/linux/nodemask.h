#ifndef __LINUX_NODEMASK_H
#define __LINUX_NODEMASK_H

#include <linux/threads.h>
#include <linux/bitmap.h>
#include <linux/minmax.h>
#include <linux/numa.h>

typedef struct { DECLARE_BITMAP(bits, MAX_NUMNODES); } nodemask_t;

enum node_states {
	N_POSSIBLE,
	N_ONLINE,
	N_NORMAL_MEMORY,
	N_HIGH_MEMORY = N_NORMAL_MEMORY,
	N_MEMORY,
	N_CPU,
	NR_NODE_STATES
};

static inline int node_state(int node, enum node_states state)
{
	return node == 0;
}


#define nr_node_ids		1U

#define node_online(node)	node_state((node), N_ONLINE)


#endif
