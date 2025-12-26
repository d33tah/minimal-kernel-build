 
 

#ifndef __LINUX_RCU_H
#define __LINUX_RCU_H

#define RCU_SEQ_CTR_SHIFT	2
#define RCU_SEQ_STATE_MASK	((1 << RCU_SEQ_CTR_SHIFT) - 1)

extern int sysctl_sched_rt_runtime;

 
static inline unsigned long rcu_seq_ctr(unsigned long s)
{
	return s >> RCU_SEQ_CTR_SHIFT;
}

 
static inline int rcu_seq_state(unsigned long s)
{
	return s & RCU_SEQ_STATE_MASK;
}

 
static inline void rcu_seq_set_state(unsigned long *sp, int newstate)
{
	WARN_ON_ONCE(newstate & ~RCU_SEQ_STATE_MASK);
	WRITE_ONCE(*sp, (*sp & ~RCU_SEQ_STATE_MASK) + newstate);
}

 
static inline void rcu_seq_start(unsigned long *sp)
{
	WRITE_ONCE(*sp, *sp + 1);
	smp_mb();  
	WARN_ON_ONCE(rcu_seq_state(*sp) != 1);
}

 
static inline unsigned long rcu_seq_endval(unsigned long *sp)
{
	return (*sp | RCU_SEQ_STATE_MASK) + 1;
}

 
static inline void rcu_seq_end(unsigned long *sp)
{
	smp_mb();  
	WARN_ON_ONCE(!rcu_seq_state(*sp));
	WRITE_ONCE(*sp, rcu_seq_endval(sp));
}

 
static inline unsigned long rcu_seq_snap(unsigned long *sp)
{
	unsigned long s;

	s = (READ_ONCE(*sp) + 2 * RCU_SEQ_STATE_MASK + 1) & ~RCU_SEQ_STATE_MASK;
	smp_mb();  
	return s;
}

 
static inline unsigned long rcu_seq_current(unsigned long *sp)
{
	return READ_ONCE(*sp);
}

/* rcu_seq_started, rcu_seq_done, rcu_seq_completed_gp, rcu_seq_new_gp, rcu_seq_diff removed - unused */

static inline int debug_rcu_head_queue(struct rcu_head *head)
{
	return 0;
}

static inline void debug_rcu_head_unqueue(struct rcu_head *head)
{
}

/* rcu_stall_is_suppressed_at_boot and rcu_stall_is_suppressed removed - unused */
#define rcu_ftrace_dump_stall_suppress()
#define rcu_ftrace_dump_stall_unsuppress()

 
#define TPS(x)  tracepoint_string(x)

 
#define rcu_ftrace_dump(oops_dump_mode) \
do { \
	static atomic_t ___rfd_beenhere = ATOMIC_INIT(0); \
	\
	if (!atomic_read(&___rfd_beenhere) && \
	    !atomic_xchg(&___rfd_beenhere, 1)) { \
		tracing_off(); \
		rcu_ftrace_dump_stall_suppress(); \
		ftrace_dump(oops_dump_mode); \
		rcu_ftrace_dump_stall_unsuppress(); \
	} \
} while (0)

void rcu_early_boot_tests(void);
void rcu_test_sync_prims(void);

 
extern void resched_cpu(int cpu);


#define RCU_FANOUT 32
#define RCU_FANOUT_LEAF 16
#define RCU_NUM_LVLS	      1
#define NUM_RCU_LVL_0	      1
#define NUM_RCU_NODES	      NUM_RCU_LVL_0
#define NUM_RCU_LVL_INIT    { NUM_RCU_LVL_0 }
/* end rcu_node_tree.h */

extern int rcu_num_lvls;
extern int num_rcu_lvl[];
extern int rcu_num_nodes;

/* rcu_init_levelspread removed - unused */

extern void rcu_init_geometry(void);

 
#define rcu_first_leaf_node() (rcu_state.level[rcu_num_lvls - 1])

 
#define rcu_is_leaf_node(rnp) ((rnp)->level == rcu_num_lvls - 1)

 
#define rcu_is_last_leaf_node(rnp) ((rnp) == &rcu_state.node[rcu_num_nodes - 1])

 
#define srcu_for_each_node_breadth_first(sp, rnp) \
	for ((rnp) = &(sp)->node[0]; \
	     (rnp) < &(sp)->node[rcu_num_nodes]; (rnp)++)
#define rcu_for_each_node_breadth_first(rnp) \
	srcu_for_each_node_breadth_first(&rcu_state, rnp)

 
#define rcu_for_each_leaf_node(rnp) \
	for ((rnp) = rcu_first_leaf_node(); \
	     (rnp) < &rcu_state.node[rcu_num_nodes]; (rnp)++)

 
#define for_each_leaf_node_possible_cpu(rnp, cpu) \
	for (WARN_ON_ONCE(!rcu_is_leaf_node(rnp)), \
	     (cpu) = cpumask_next((rnp)->grplo - 1, cpu_possible_mask); \
	     (cpu) <= rnp->grphi; \
	     (cpu) = cpumask_next((cpu), cpu_possible_mask))

 
#define rcu_find_next_bit(rnp, cpu, mask) \
	((rnp)->grplo + find_next_bit(&(mask), BITS_PER_LONG, (cpu)))
#define for_each_leaf_node_cpu_mask(rnp, cpu, mask) \
	for (WARN_ON_ONCE(!rcu_is_leaf_node(rnp)), \
	     (cpu) = rcu_find_next_bit((rnp), 0, (mask)); \
	     (cpu) <= rnp->grphi; \
	     (cpu) = rcu_find_next_bit((rnp), (cpu) + 1 - (rnp->grplo), (mask)))

 
#define raw_spin_lock_rcu_node(p)					\
do {									\
	raw_spin_lock(&ACCESS_PRIVATE(p, lock));			\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_rcu_node(p)					\
do {									\
	lockdep_assert_irqs_disabled();					\
	raw_spin_unlock(&ACCESS_PRIVATE(p, lock));			\
} while (0)

#define raw_spin_lock_irq_rcu_node(p)					\
do {									\
	raw_spin_lock_irq(&ACCESS_PRIVATE(p, lock));			\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_irq_rcu_node(p)					\
do {									\
	lockdep_assert_irqs_disabled();					\
	raw_spin_unlock_irq(&ACCESS_PRIVATE(p, lock));			\
} while (0)

#define raw_spin_lock_irqsave_rcu_node(p, flags)			\
do {									\
	raw_spin_lock_irqsave(&ACCESS_PRIVATE(p, lock), flags);	\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_irqrestore_rcu_node(p, flags)			\
do {									\
	lockdep_assert_irqs_disabled();					\
	raw_spin_unlock_irqrestore(&ACCESS_PRIVATE(p, lock), flags);	\
} while (0)

#define raw_spin_trylock_rcu_node(p)					\
({									\
	bool ___locked = raw_spin_trylock(&ACCESS_PRIVATE(p, lock));	\
									\
	if (___locked)							\
		smp_mb__after_unlock_lock();				\
	___locked;							\
})

#define raw_lockdep_assert_held_rcu_node(p)				\
	lockdep_assert_held(&ACCESS_PRIVATE(p, lock))

/* rcu_gp_is_normal, rcu_gp_is_expedited, rcu_expedite_gp, rcu_unexpedite_gp, rcu_request_urgent_qs_task removed - unused */

#define RCU_SCHEDULER_INACTIVE	0
#define RCU_SCHEDULER_INIT	1
#define RCU_SCHEDULER_RUNNING	2

enum rcutorture_type {
	RCU_FLAVOR,
	RCU_TASKS_FLAVOR,
	RCU_TASKS_RUDE_FLAVOR,
	RCU_TASKS_TRACING_FLAVOR,
	RCU_TRIVIAL_FLAVOR,
	SRCU_FLAVOR,
	INVALID_RCU_FLAVOR
};

/* Removed uncalled: rcutorture_get_gp_data, do_trace_rcu_torture_read,
 * rcu_gp_set_torture_wait, srcutorture_get_gp_data, rcu_dynticks_zero_in_eqs,
 * rcu_get_gp_seq, rcu_exp_batches_completed, srcu_batches_completed,
 * rcu_force_quiescent_state, rcu_check_boost_fail, show_rcu_gp_kthreads,
 * rcu_get_gp_kthreads_prio, rcu_fwd_progress_check, rcu_gp_slow_register,
 * rcu_gp_slow_unregister, rcu_bind_current_to_nocb, show_rcu_tasks_classic_gp_kthread,
 * show_rcu_tasks_rude_gp_kthread, show_rcu_tasks_trace_gp_kthread */

#endif  
