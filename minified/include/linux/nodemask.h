#ifndef __LINUX_NODEMASK_H
#define __LINUX_NODEMASK_H

#include <linux/threads.h>
#include <linux/bitmap.h>
#include <linux/minmax.h>
#include <linux/numa.h>

typedef struct { DECLARE_BITMAP(bits, MAX_NUMNODES); } nodemask_t;

/* node_test_and_set, nodes_and, nodes_or, nodes_andnot, nodes_equal,
 * nodes_intersects, nodes_subset, nodes_empty removed - unused */

/* first_node, next_node, next_node_in, init_nodemask_of_node,
 * nodemask_of_node removed - unused with MAX_NUMNODES=1 */

enum node_states {
	N_POSSIBLE,
	N_ONLINE,
	N_NORMAL_MEMORY,
	N_HIGH_MEMORY = N_NORMAL_MEMORY,
	N_MEMORY,
	N_CPU,
	NR_NODE_STATES
};

/* MAX_NUMNODES=1, simplified node state functions */
static inline int node_state(int node, enum node_states state)
{
	return node == 0;
}

#define for_each_node_state(node, __state) \
	for ( (node) = 0; (node) == 0; (node) = 1)

#define first_online_node	0
#define next_online_node(nid)	(MAX_NUMNODES)
#define nr_node_ids		1U
#define nr_online_nodes		1U

#define node_online(node)	node_state((node), N_ONLINE)

#define for_each_node(node)	   for_each_node_state(node, N_POSSIBLE)

#endif
