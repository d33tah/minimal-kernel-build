 
 

#ifndef __LINUX_RCU_H
#define __LINUX_RCU_H

/* RCU_SEQ_CTR_SHIFT, RCU_SEQ_STATE_MASK removed - never referenced (TinyRCU) */

/* rcu_seq_ctr/set_state/start/end/endval/snap/current/started/done,
 * rcu_seq_completed_gp, rcu_seq_new_gp, rcu_seq_diff removed - unused */

static inline void debug_rcu_head_unqueue(struct rcu_head *head)
{
}

/* rcu_stall_is_suppressed_at_boot and rcu_stall_is_suppressed removed - unused */
/* rcu_ftrace_dump_stall_suppress/_unsuppress + rcu_ftrace_dump removed - unused */


extern void resched_cpu(int cpu);


/* RCU_FANOUT/RCU_FANOUT_LEAF/RCU_NUM_LVLS/NUM_RCU_LVL_0/NUM_RCU_NODES/
 * NUM_RCU_LVL_INIT removed - rcu_node tree unused in TinyRCU, never referenced */
/* end rcu_node_tree.h */


/* rcu_init_levelspread removed - unused */

/* rcu_gp_is_normal, rcu_gp_is_expedited, rcu_expedite_gp, rcu_unexpedite_gp, rcu_request_urgent_qs_task removed - unused */
/* rcu_scheduler_active + RCU_SCHEDULER_{INACTIVE,INIT,RUNNING} removed - write-only state, never read */
/* enum rcutorture_type + rcutorture_get_gp_data/do_trace_rcu_torture_read removed - 0-ref tree-wide (CONFIG_TREE_RCU unset) */

#endif
