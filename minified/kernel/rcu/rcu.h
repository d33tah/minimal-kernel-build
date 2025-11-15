 
 

#ifndef __LINUX_RCU_H
#define __LINUX_RCU_H


 
#define DYNTICK_IRQ_NONIDLE	((LONG_MAX / 2) + 1)


 

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

 
static inline bool rcu_seq_started(unsigned long *sp, unsigned long s)
{
	return ULONG_CMP_LT((s - 1) & ~RCU_SEQ_STATE_MASK, READ_ONCE(*sp));
}

 
static inline bool rcu_seq_done(unsigned long *sp, unsigned long s)
{
	return ULONG_CMP_GE(READ_ONCE(*sp), s);
}

 
static inline bool rcu_seq_completed_gp(unsigned long old, unsigned long new)
{
	return ULONG_CMP_LT(old, new & ~RCU_SEQ_STATE_MASK);
}

 
static inline bool rcu_seq_new_gp(unsigned long old, unsigned long new)
{
	return ULONG_CMP_LT((old + RCU_SEQ_STATE_MASK) & ~RCU_SEQ_STATE_MASK,
			    new);
}

 
static inline unsigned long rcu_seq_diff(unsigned long new, unsigned long old)
{
	unsigned long rnd_diff;

	if (old == new)
		return 0;
	 
	rnd_diff = (new & ~RCU_SEQ_STATE_MASK) -
		   ((old + RCU_SEQ_STATE_MASK) & ~RCU_SEQ_STATE_MASK) +
		   ((new & RCU_SEQ_STATE_MASK) || (old & RCU_SEQ_STATE_MASK));
	if (ULONG_CMP_GE(RCU_SEQ_STATE_MASK, rnd_diff))
		return 1;  
	return ((rnd_diff - RCU_SEQ_STATE_MASK - 1) >> RCU_SEQ_CTR_SHIFT) + 2;
}

 

static inline int debug_rcu_head_queue(struct rcu_head *head)
{
	return 0;
}

static inline void debug_rcu_head_unqueue(struct rcu_head *head)
{
}

extern int rcu_cpu_stall_suppress_at_boot;

static inline bool rcu_stall_is_suppressed_at_boot(void)
{
	return rcu_cpu_stall_suppress_at_boot && !rcu_inkernel_boot_has_ended();
}


static inline bool rcu_stall_is_suppressed(void)
{
	return rcu_stall_is_suppressed_at_boot();
}
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


#include <linux/rcu_node_tree.h>

extern int rcu_num_lvls;
extern int num_rcu_lvl[];
extern int rcu_num_nodes;
static bool rcu_fanout_exact;
static int rcu_fanout_leaf;

 
static inline void rcu_init_levelspread(int *levelspread, const int *levelcnt)
{
	int i;

	for (i = 0; i < RCU_NUM_LVLS; i++)
		levelspread[i] = INT_MIN;
	if (rcu_fanout_exact) {
		levelspread[rcu_num_lvls - 1] = rcu_fanout_leaf;
		for (i = rcu_num_lvls - 2; i >= 0; i--)
			levelspread[i] = RCU_FANOUT;
	} else {
		int ccur;
		int cprv;

		cprv = nr_cpu_ids;
		for (i = rcu_num_lvls - 1; i >= 0; i--) {
			ccur = levelcnt[i];
			levelspread[i] = (cprv + ccur - 1) / ccur;
			cprv = ccur;
		}
	}
}

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


 
static inline bool rcu_gp_is_normal(void) { return true; }
static inline bool rcu_gp_is_expedited(void) { return false; }
static inline void rcu_expedite_gp(void) { }
static inline void rcu_unexpedite_gp(void) { }
static inline void rcu_request_urgent_qs_task(struct task_struct *t) { }

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

#if defined(CONFIG_TREE_RCU)
void rcutorture_get_gp_data(enum rcutorture_type test_type, int *flags,
			    unsigned long *gp_seq);
void do_trace_rcu_torture_read(const char *rcutorturename,
			       struct rcu_head *rhp,
			       unsigned long secs,
			       unsigned long c_old,
			       unsigned long c);
void rcu_gp_set_torture_wait(int duration);
#else
static inline void rcutorture_get_gp_data(enum rcutorture_type test_type,
					  int *flags, unsigned long *gp_seq)
{
	*flags = 0;
	*gp_seq = 0;
}
#define do_trace_rcu_torture_read(rcutorturename, rhp, secs, c_old, c) \
	do { } while (0)
static inline void rcu_gp_set_torture_wait(int duration) { }
#endif

#if IS_ENABLED(CONFIG_RCU_TORTURE_TEST) || IS_MODULE(CONFIG_RCU_TORTURE_TEST)
long rcutorture_sched_setaffinity(pid_t pid, const struct cpumask *in_mask);
#endif


static inline void srcutorture_get_gp_data(enum rcutorture_type test_type,
					   struct srcu_struct *sp, int *flags,
					   unsigned long *gp_seq)
{
	if (test_type != SRCU_FLAVOR)
		return;
	*flags = 0;
	*gp_seq = sp->srcu_idx;
}


static inline bool rcu_dynticks_zero_in_eqs(int cpu, int *vp) { return false; }
static inline unsigned long rcu_get_gp_seq(void) { return 0; }
static inline unsigned long rcu_exp_batches_completed(void) { return 0; }
static inline unsigned long
srcu_batches_completed(struct srcu_struct *sp) { return 0; }
static inline void rcu_force_quiescent_state(void) { }
static inline bool rcu_check_boost_fail(unsigned long gp_state, int *cpup) { return true; }
static inline void show_rcu_gp_kthreads(void) { }
static inline int rcu_get_gp_kthreads_prio(void) { return 0; }
static inline void rcu_fwd_progress_check(unsigned long j) { }
static inline void rcu_gp_slow_register(atomic_t *rgssp) { }
static inline void rcu_gp_slow_unregister(atomic_t *rgssp) { }

static inline void rcu_bind_current_to_nocb(void) { }

static inline void show_rcu_tasks_classic_gp_kthread(void) {}
static inline void show_rcu_tasks_rude_gp_kthread(void) {}
static inline void show_rcu_tasks_trace_gp_kthread(void) {}

#endif  
