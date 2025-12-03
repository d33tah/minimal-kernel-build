#ifndef __LINUX_NODEMASK_H
#define __LINUX_NODEMASK_H


#include <linux/threads.h>
#include <linux/bitmap.h>
#include <linux/minmax.h>
#include <linux/numa.h>

typedef struct { DECLARE_BITMAP(bits, MAX_NUMNODES); } nodemask_t;
extern nodemask_t _unused_nodemask_arg_;

#define nodemask_pr_args(maskp)	__nodemask_pr_numnodes(maskp), \
				__nodemask_pr_bits(maskp)
static inline unsigned int __nodemask_pr_numnodes(const nodemask_t *m)
{
	return m ? MAX_NUMNODES : 0;
}
static inline const unsigned long *__nodemask_pr_bits(const nodemask_t *m)
{
	return m ? m->bits : NULL;
}

#define node_set(node, dst) __node_set((node), &(dst))
static __always_inline void __node_set(int node, volatile nodemask_t *dstp)
{
	set_bit(node, dstp->bits);
}

/* node_clear, nodes_setall removed - unused */

#define nodes_clear(dst) __nodes_clear(&(dst), MAX_NUMNODES)
static inline void __nodes_clear(nodemask_t *dstp, unsigned int nbits)
{
	bitmap_zero(dstp->bits, nbits);
}

#define node_isset(node, nodemask) test_bit((node), (nodemask).bits)

/* node_test_and_set, nodes_and, nodes_or, nodes_andnot, nodes_equal,
 * nodes_intersects, nodes_subset removed - unused */

#define nodes_empty(src) __nodes_empty(&(src), MAX_NUMNODES)
static inline bool __nodes_empty(const nodemask_t *srcp, unsigned int nbits)
{
	return bitmap_empty(srcp->bits, nbits);
}

#define nodes_weight(nodemask) __nodes_weight(&(nodemask), MAX_NUMNODES)
static inline int __nodes_weight(const nodemask_t *srcp, unsigned int nbits)
{
	return bitmap_weight(srcp->bits, nbits);
}



/* first_node, next_node, next_node_in, init_nodemask_of_node,
 * nodemask_of_node removed - unused with MAX_NUMNODES=1 */

#define NODE_MASK_LAST_WORD BITMAP_LAST_WORD_MASK(MAX_NUMNODES)

/* MAX_NUMNODES == 1 <= BITS_PER_LONG always */
#define NODE_MASK_ALL							\
((nodemask_t) { {							\
	[BITS_TO_LONGS(MAX_NUMNODES)-1] = NODE_MASK_LAST_WORD		\
} })

#define NODE_MASK_NONE							\
((nodemask_t) { {							\
	[0 ... BITS_TO_LONGS(MAX_NUMNODES)-1] =  0UL			\
} })

#define nodes_addr(src) ((src).bits)

/* node_remap removed - unused */

/* MAX_NUMNODES=1, use simplified version */
#define for_each_node_mask(node, mask)                                  \
	for ((node) = 0; (node) < 1 && !nodes_empty(mask); (node)++)  

enum node_states {
	N_POSSIBLE,		 
	N_ONLINE,		 
	N_NORMAL_MEMORY,	 
	N_HIGH_MEMORY = N_NORMAL_MEMORY,
	N_MEMORY,		 
	N_CPU,		 
	N_GENERIC_INITIATOR,	 
	NR_NODE_STATES
};


extern nodemask_t node_states[NR_NODE_STATES];

/* MAX_NUMNODES=1, simplified node state functions */
static inline int node_state(int node, enum node_states state)
{
	return node == 0;
}

static inline void node_set_state(int node, enum node_states state)
{
}

static inline void node_clear_state(int node, enum node_states state)
{
}

static inline int num_node_state(enum node_states state)
{
	return 1;
}

#define for_each_node_state(node, __state) \
	for ( (node) = 0; (node) == 0; (node) = 1)

#define first_online_node	0
#define first_memory_node	0
#define next_online_node(nid)	(MAX_NUMNODES)
#define nr_node_ids		1U
#define nr_online_nodes		1U

#define node_set_online(node)	   node_set_state((node), N_ONLINE)
#define node_set_offline(node)	   node_clear_state((node), N_ONLINE)

#define node_online_map 	node_states[N_ONLINE]
#define node_possible_map 	node_states[N_POSSIBLE]

#define num_online_nodes()	num_node_state(N_ONLINE)
#define num_possible_nodes()	num_node_state(N_POSSIBLE)
#define node_online(node)	node_state((node), N_ONLINE)
#define node_possible(node)	node_state((node), N_POSSIBLE)

#define for_each_node(node)	   for_each_node_state(node, N_POSSIBLE)
#define for_each_online_node(node) for_each_node_state(node, N_ONLINE)

#if NODES_SHIFT > 8  
#define NODEMASK_ALLOC(type, name, gfp_flags)	\
			type *name = kmalloc(sizeof(*name), gfp_flags)
#define NODEMASK_FREE(m)			kfree(m)
#else
#define NODEMASK_ALLOC(type, name, gfp_flags)	type _##name, *name = &_##name
#define NODEMASK_FREE(m)			do {} while (0)
#endif


#endif
