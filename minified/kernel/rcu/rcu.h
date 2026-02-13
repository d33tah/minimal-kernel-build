 
#ifndef __LINUX_RCU_H
#define __LINUX_RCU_H

/* RCU sequence functions removed - unused:
 * rcu_seq_ctr, rcu_seq_state, rcu_seq_set_state, rcu_seq_start,
 * rcu_seq_endval, rcu_seq_end, rcu_seq_snap, rcu_seq_current,
 * rcu_seq_started, rcu_seq_done, rcu_seq_completed_gp, rcu_seq_new_gp, rcu_seq_diff */

extern void resched_cpu(int cpu);

/* Many RCU macros and enums removed - unused in minimal kernel:
 * TPS, rcu_ftrace_dump, RCU_FANOUT, RCU_FANOUT_LEAF, RCU_NUM_LVLS,
 * NUM_RCU_LVL_0, NUM_RCU_NODES, NUM_RCU_LVL_INIT, rcu_first_leaf_node,
 * rcu_is_leaf_node, rcu_is_last_leaf_node, srcu_for_each_node_breadth_first,
 * rcu_for_each_node_breadth_first, rcu_for_each_leaf_node,
 * for_each_leaf_node_possible_cpu, rcu_find_next_bit, for_each_leaf_node_cpu_mask,
 * raw_spin_*_rcu_node macros, rcutorture_type enum */

#endif
