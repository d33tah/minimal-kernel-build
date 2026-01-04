 
 

#ifndef __LINUX_RCU_H
#define __LINUX_RCU_H

/* RCU sequence functions removed - unused:
 * rcu_seq_ctr, rcu_seq_state, rcu_seq_set_state, rcu_seq_start,
 * rcu_seq_endval, rcu_seq_end, rcu_seq_snap, rcu_seq_current,
 * rcu_seq_started, rcu_seq_done, rcu_seq_completed_gp, rcu_seq_new_gp, rcu_seq_diff */

extern int sysctl_sched_rt_runtime;

/* debug_rcu_head_queue, debug_rcu_head_unqueue removed - never called */
/* rcu_stall_is_suppressed_at_boot and rcu_stall_is_suppressed removed - unused */
/* rcu_ftrace_dump_stall_suppress, rcu_ftrace_dump_stall_unsuppress removed - unused */

void rcu_early_boot_tests(void);
void rcu_test_sync_prims(void);

extern void resched_cpu(int cpu);
extern int rcu_num_lvls;
extern int num_rcu_lvl[];
extern int rcu_num_nodes;

#define RCU_SCHEDULER_INACTIVE	0
#define RCU_SCHEDULER_INIT	1
#define RCU_SCHEDULER_RUNNING	2

/* Many RCU macros and enums removed - unused in minimal kernel:
 * TPS, rcu_ftrace_dump, RCU_FANOUT, RCU_FANOUT_LEAF, RCU_NUM_LVLS,
 * NUM_RCU_LVL_0, NUM_RCU_NODES, NUM_RCU_LVL_INIT, rcu_first_leaf_node,
 * rcu_is_leaf_node, rcu_is_last_leaf_node, srcu_for_each_node_breadth_first,
 * rcu_for_each_node_breadth_first, rcu_for_each_leaf_node,
 * for_each_leaf_node_possible_cpu, rcu_find_next_bit, for_each_leaf_node_cpu_mask,
 * raw_spin_*_rcu_node macros, rcutorture_type enum */

#endif
