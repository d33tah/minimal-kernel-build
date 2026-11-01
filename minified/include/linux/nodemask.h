/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_NODEMASK_H
#define __LINUX_NODEMASK_H

#include <linux/bitmap.h>
#include <linux/numa.h>

typedef struct {
	DECLARE_BITMAP(bits, MAX_NUMNODES);
} nodemask_t;

/*
 * The inline keyword gives the compiler room to decide to inline, or
 * not inline a function as it sees best.  However, as these functions
 * are called in both __init and non-__init functions, if they are not
 * inlined we will end up with a section mismatch error (of the type of
 * freeable items not being freed).  So we must use __always_inline here
 * to fix the problem.  If other functions in the future also end up in
 * this situation they will also need to be annotated as __always_inline
 */
#define node_set(node, dst) __node_set((node), &(dst))
static __always_inline void __node_set(int node, volatile nodemask_t *dstp)
{
	set_bit(node, dstp->bits);
}

#define nodes_empty(src) __nodes_empty(&(src), MAX_NUMNODES)
static inline bool __nodes_empty(const nodemask_t *srcp, unsigned int nbits)
{
	return bitmap_empty(srcp->bits, nbits);
}

#define for_each_node_mask(node, mask) \
	for ((node) = 0; (node) < 1 && !nodes_empty(mask); (node)++)

/*
 * Bitmasks that are kept for all the nodes.
 */
enum node_states {
	N_NORMAL_MEMORY, /* The node has regular memory */
};

#define for_each_node_state(node, __state) \
	for ((node) = 0; (node) == 0; (node) = 1)

#define nr_node_ids 1U

#endif
