/* RCU node tree definitions - simplified for NR_CPUS=1 */
#ifndef __LINUX_RCU_NODE_TREE_H
#define __LINUX_RCU_NODE_TREE_H

#define RCU_FANOUT 32
#define RCU_FANOUT_LEAF 16

/* NR_CPUS=1 <= RCU_FANOUT_LEAF=16, so only one level needed */
#define RCU_NUM_LVLS	      1
#define NUM_RCU_LVL_0	      1
#define NUM_RCU_NODES	      NUM_RCU_LVL_0
#define NUM_RCU_LVL_INIT    { NUM_RCU_LVL_0 }
#define RCU_NODE_NAME_INIT  { "rcu_node_0" }
#define RCU_FQS_NAME_INIT   { "rcu_node_fqs_0" }

#endif
