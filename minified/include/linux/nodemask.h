#ifndef __LINUX_NODEMASK_H
#define __LINUX_NODEMASK_H

#include <linux/bitmap.h>
#include <linux/numa.h>

typedef struct {
	DECLARE_BITMAP(bits, MAX_NUMNODES);
} nodemask_t;

#define node_set(node, dst) __node_set((node), &(dst))
static __always_inline void __node_set(int node, volatile nodemask_t *dstp)
{
	set_bit(node, dstp->bits);
}

/* node_test_and_set, nodes_and, nodes_or, nodes_andnot, nodes_equal,
 * nodes_intersects, nodes_subset removed - unused */

#define nodes_empty(src) __nodes_empty(&(src), MAX_NUMNODES)
static inline bool __nodes_empty(const nodemask_t *srcp, unsigned int nbits)
{
	return bitmap_empty(srcp->bits, nbits);
}

/* first_node, next_node, next_node_in, init_nodemask_of_node,
 * nodemask_of_node removed - unused with MAX_NUMNODES=1 */

/* MAX_NUMNODES=1, use simplified version */
#define for_each_node_mask(node, mask) \
	for ((node) = 0; (node) < 1 && !nodes_empty(mask); (node)++)

enum node_states {
	N_POSSIBLE,
	N_NORMAL_MEMORY,
};

#define for_each_node_state(node, __state) \
	for ((node) = 0; (node) == 0; (node) = 1)

#define nr_node_ids 1U

#define for_each_node(node) for_each_node_state(node, N_POSSIBLE)

#endif
