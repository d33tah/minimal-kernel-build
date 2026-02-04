 
 
#ifndef _KERNEL_SCHED_SCHED_H
#define _KERNEL_SCHED_SCHED_H

/* SCHED_CPUFREQ_IOWAIT removed - unused */
#include <linux/sched.h>
/* dl_prio, dl_task removed - always returned false */
#define MAX_DL_PRIO 0
/* loadavg.h inlined - LOAD_FREQ is the only macro used */
#define LOAD_FREQ (5*HZ+1)
#include <linux/sched/mm.h>

#include <linux/sched/signal.h>
/* extern total_forks removed - only incremented, never read */
extern int nr_threads;
/* process_counts, nr_running, single_task_running, nr_iowait, nr_iowait_cpu removed/made static */
/* end sched/stat.h */
/* linux/sched/sysctl.h removed - sysctl_hung_task_timeout_secs unused */

#include <linux/sched/task.h>
/* arch_scale_thermal_pressure removed - unused */

#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/bug.h>
#include <linux/capability.h>

#include <linux/cgroup.h>
#include <linux/ctype.h>
#include <linux/file.h>


#include <linux/interrupt.h>
/* irq_work.h removed - header is now empty */
#include <linux/jiffies.h>

#include <linux/kthread.h>


#include <linux/lockdep.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/module.h>

/* plist.h removed - unused */
#include <linux/poll.h>
/* proc_fs.h removed - empty header */
#include <linux/rcupdate.h>
/* seq_file.h removed - header is empty */
#include <linux/seqlock.h>


#include <linux/jump_label.h>
#include <linux/stop_machine.h>

#include <linux/syscalls.h>
#include <linux/topology.h>
#include <linux/types.h>

#include <linux/uaccess.h>

#include <linux/wait_bit.h>



/* workqueue_internal.h removed - empty header */

/* cpupri.h and cpudeadline.h removed - unused structs and macros */

# define SCHED_WARN_ON(x)      ({ (void)(x), 0; })

struct rq;
/* struct cpuidle_state forward decl removed - never defined or used */

 
#define TASK_ON_RQ_QUEUED	1
#define TASK_ON_RQ_MIGRATING	2

/* scheduler_running, calc_load_update removed - write-only variables */
/* calc_load_tasks, sysctl_sched_child_runs_first removed - never used */
/* calc_global_load_tick, calc_load_fold_active removed - never called */

/* NS_TO_JIFFIES, NICE_0_LOAD, RUNTIME_INF removed - never used */
# define NICE_0_LOAD_SHIFT	(SCHED_FIXEDPOINT_SHIFT)
# define scale_load(w)		(w)
# define scale_load_down(w)	(w)

static inline int idle_policy(int policy)
{
	return policy == SCHED_IDLE;
}
static inline int fair_policy(int policy)
{
	return policy == SCHED_NORMAL || policy == SCHED_BATCH;
}

static inline int rt_policy(int policy)
{
	return policy == SCHED_FIFO || policy == SCHED_RR;
}

static inline int dl_policy(int policy)
{
	return policy == SCHED_DEADLINE;
}
/* valid_policy inlined at core.c - single caller */

static inline int task_has_idle_policy(struct task_struct *p)
{
	return idle_policy(p->policy);
}

static inline int task_has_rt_policy(struct task_struct *p)
{
	return rt_policy(p->policy);
}

static inline int task_has_dl_policy(struct task_struct *p)
{
	return dl_policy(p->policy);
}

/* cap_scale, shr_bound removed - unused */
/* struct rt_prio_array removed - rt_rq.active was the only user, now removed */
/* struct rt_bandwidth removed - no longer used */

/* __dl_clear_params removed - empty stub */
/* struct dl_bandwidth removed - unused */

/* struct dl_bw removed - dl_rq never read, only initialized */

/* init_dl_bw, sched_dl_global_validate, sched_dl_do_global, sched_dl_overflow,
   __getparam_dl, __checkparam_dl, __setparam_dl, dl_param_changed, dl_cpuset_cpumask_can_shrink,
   dl_cpu_busy removed - never defined/called */

/* struct cfs_bandwidth removed - unused */

struct cfs_rq {
	struct load_weight	load;
	unsigned int		nr_running;
	unsigned int		h_nr_running;
	/* idle_nr_running, idle_h_nr_running, exec_clock, min_vruntime_copy removed - write-only */
	u64			min_vruntime;
	struct rb_root_cached	tasks_timeline;


	struct sched_entity	*curr;
	/* next, last, skip removed - buddy tracking removed for single-task kernel */



};


struct rt_rq {
	/* All fields removed - rt_rq never read, only initialized */
	char dummy; /* Empty struct not allowed in C */
};

struct dl_rq {
	/* All fields removed - dl_rq never read, only initialized */
	char dummy; /* Empty struct not allowed in C */
};

#define entity_is_task(se)	1
/* se_update_runnable removed - empty stub */
struct rq {
	 
	raw_spinlock_t		__lock;

	 
	unsigned int		nr_running;

	struct cfs_rq		cfs;
	struct rt_rq		rt;
	struct dl_rq		dl;


	/* nr_uninterruptible removed - only modified, never read */

	struct task_struct __rcu	*curr;
	struct task_struct	*idle;
	struct task_struct	*stop;
	struct mm_struct	*prev_mm;

	unsigned int		clock_update_flags;
	u64			clock;
	u64			clock_task ____cacheline_aligned;

	/* nr_iowait removed - only incremented/decremented, never read */
};


static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return container_of(cfs_rq, struct rq, cfs);
}

/* cpu_of removed - never called, always returns 0 in UP config */

/* struct sched_group forward decl removed - never defined or used */
/* sched_core_enabled removed - unused */

static inline bool sched_core_disabled(void)
{
	return true;
}

static inline raw_spinlock_t *rq_lockp(struct rq *rq)
{
	return &rq->__lock;
}
/* __rq_lockp removed - duplicate of rq_lockp, never called */

/* lockdep_assert_rq_held removed - was empty stub */

extern void raw_spin_rq_lock_nested(struct rq *rq, int subclass);
/* extern bool raw_spin_rq_trylock(struct rq *rq); removed - never called */
extern void raw_spin_rq_unlock(struct rq *rq);

static inline void raw_spin_rq_lock(struct rq *rq)
{
	raw_spin_rq_lock_nested(rq, 0);
}

static inline void raw_spin_rq_lock_irq(struct rq *rq)
{
	local_irq_disable();
	raw_spin_rq_lock(rq);
}

static inline void raw_spin_rq_unlock_irq(struct rq *rq)
{
	raw_spin_rq_unlock(rq);
	local_irq_enable();
}

static inline unsigned long _raw_spin_rq_lock_irqsave(struct rq *rq)
{
	unsigned long flags;
	local_irq_save(flags);
	raw_spin_rq_lock(rq);
	return flags;
}

static inline void raw_spin_rq_unlock_irqrestore(struct rq *rq, unsigned long flags)
{
	raw_spin_rq_unlock(rq);
	local_irq_restore(flags);
}

#define raw_spin_rq_lock_irqsave(rq, flags)	\
do {						\
	flags = _raw_spin_rq_lock_irqsave(rq);	\
} while (0)
/* update_idle_core removed - empty stub */
DECLARE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);

#define cpu_rq(cpu)		(&per_cpu(runqueues, (cpu)))
#define this_rq()		this_cpu_ptr(&runqueues)
#define task_rq(p)		cpu_rq(task_cpu(p))
#define cpu_curr(cpu)		(cpu_rq(cpu)->curr)
#define raw_rq()		raw_cpu_ptr(&runqueues)


static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}

static inline struct cfs_rq *task_cfs_rq(struct task_struct *p)
{
	return &task_rq(p)->cfs;
}

static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
	struct task_struct *p = task_of(se);
	struct rq *rq = task_rq(p);

	return &rq->cfs;
}

/* group_cfs_rq removed - always returned NULL, no callers */

extern void update_rq_clock(struct rq *rq);

 
#define RQCF_REQ_SKIP		0x01
#define RQCF_ACT_SKIP		0x02

static inline void assert_clock_updated(struct rq *rq)
{

	SCHED_WARN_ON(rq->clock_update_flags < RQCF_ACT_SKIP);
}

/* rq_clock removed - unused */

static inline u64 rq_clock_task(struct rq *rq)
{
	assert_clock_updated(rq);

	return rq->clock_task;
}

/* rq_clock_thermal removed - unused */

static inline void rq_clock_skip_update(struct rq *rq)
{
	rq->clock_update_flags |= RQCF_REQ_SKIP;
}

/* rq_clock_cancel_skipupdate removed - unused */

struct rq_flags {
	unsigned long flags;
};


/* rq_pin_lock, rq_unpin_lock removed - lockdep disabled, empty bodies */
#define rq_pin_lock(rq, rf) do { } while (0)
#define rq_unpin_lock(rq, rf) do { } while (0)

struct rq *__task_rq_lock(struct task_struct *p, struct rq_flags *rf)
	__acquires(rq->lock);

struct rq *task_rq_lock(struct task_struct *p, struct rq_flags *rf)
	__acquires(p->pi_lock)
	__acquires(rq->lock);

static inline void __task_rq_unlock(struct rq *rq, struct rq_flags *rf)
	__releases(rq->lock)
{
	rq_unpin_lock(rq, rf);
	raw_spin_rq_unlock(rq);
}

static inline void
task_rq_unlock(struct rq *rq, struct task_struct *p, struct rq_flags *rf)
	__releases(rq->lock)
	__releases(p->pi_lock)
{
	rq_unpin_lock(rq, rf);
	raw_spin_rq_unlock(rq);
	raw_spin_unlock_irqrestore(&p->pi_lock, rf->flags);
}

static inline void
rq_lock_irqsave(struct rq *rq, struct rq_flags *rf)
	__acquires(rq->lock)
{
	raw_spin_rq_lock_irqsave(rq, rf->flags);
	rq_pin_lock(rq, rf);
}

static inline void
rq_lock_irq(struct rq *rq, struct rq_flags *rf)
	__acquires(rq->lock)
{
	raw_spin_rq_lock_irq(rq);
	rq_pin_lock(rq, rf);
}

static inline void
rq_lock(struct rq *rq, struct rq_flags *rf)
	__acquires(rq->lock)
{
	raw_spin_rq_lock(rq);
	rq_pin_lock(rq, rf);
}

static inline void
rq_unlock_irqrestore(struct rq *rq, struct rq_flags *rf)
	__releases(rq->lock)
{
	rq_unpin_lock(rq, rf);
	raw_spin_rq_unlock_irqrestore(rq, rf->flags);
}

static inline void
rq_unlock_irq(struct rq *rq, struct rq_flags *rf)
	__releases(rq->lock)
{
	rq_unpin_lock(rq, rf);
	raw_spin_rq_unlock_irq(rq);
}

static inline void
rq_unlock(struct rq *rq, struct rq_flags *rf)
	__releases(rq->lock)
{
	rq_unpin_lock(rq, rf);
	raw_spin_rq_unlock(rq);
}

/* this_rq_lock_irq removed - unused */

/* sched_init_numa, sched_update_numa, sched_domains_numa_masks_set/clear, sched_numa_find_closest, init_numa_balancing removed - unused */

/* stats.h removed - was empty header */


/* sched_core_account_forceidle removed - unused */

/* sched_core_tick, set_task_rq, task_group removed - unused/empty stubs */

static inline void __set_task_cpu(struct task_struct *p, unsigned int cpu)
{
	/* set_task_rq removed - empty stub */
}

 
# define const_debug const

#define SCHED_FEAT(name, enabled)	\
	__SCHED_FEAT_##name ,

enum {
#include "features.h"
	__SCHED_FEAT_NR,
};

#undef SCHED_FEAT


 
#define SCHED_FEAT(name, enabled)	\
	(1UL << __SCHED_FEAT_##name) * enabled |
static const_debug __maybe_unused unsigned int sysctl_sched_features =
#include "features.h"
	0;
#undef SCHED_FEAT

#define sched_feat(x) !!(sysctl_sched_features & (1UL << __SCHED_FEAT_##x))

/* sched_numa_balancing removed - defined but never used */

/* global_rt_period and global_rt_runtime inlined at core.c - single caller */

static inline int task_current(struct rq *rq, struct task_struct *p)
{
	return rq->curr == p;
}

static inline int task_on_rq_queued(struct task_struct *p)
{
	return p->on_rq == TASK_ON_RQ_QUEUED;
}

/* task_on_rq_migrating removed - never called (single-CPU) */

#define WF_FORK     0x04
/* WF_SYNC removed - unused */

#define WEIGHT_IDLEPRIO		3
#define WMULT_IDLEPRIO		1431655765

extern const int		sched_prio_to_weight[40];
extern const u32		sched_prio_to_wmult[40];

 

#define DEQUEUE_SLEEP		0x01
#define DEQUEUE_SAVE		0x02  
#define DEQUEUE_MOVE		0x04  
#define DEQUEUE_NOCLOCK		0x08  

#define ENQUEUE_WAKEUP		0x01
/* ENQUEUE_RESTORE removed - never used */
#define ENQUEUE_NOCLOCK		0x08
#define ENQUEUE_HEAD		0x10

/* RETRY_TASK, ENQUEUE_MIGRATED removed - never used */

struct sched_class {


	void (*enqueue_task) (struct rq *rq, struct task_struct *p, int flags);
	void (*dequeue_task) (struct rq *rq, struct task_struct *p, int flags);
	/* yield_task removed - callback never called */
	/* yield_to_task removed - callback never called */

	void (*check_preempt_curr)(struct rq *rq, struct task_struct *p, int flags);

	struct task_struct *(*pick_next_task)(struct rq *rq);

	void (*put_prev_task)(struct rq *rq, struct task_struct *p);
	void (*set_next_task)(struct rq *rq, struct task_struct *p, bool first);


	void (*task_tick)(struct rq *rq, struct task_struct *p, int queued);
	void (*task_fork)(struct task_struct *p);
	/* task_dead removed - no sched class defines it */

	 
	void (*switched_from)(struct rq *this_rq, struct task_struct *task);
	void (*switched_to)  (struct rq *this_rq, struct task_struct *task);
	void (*prio_changed) (struct rq *this_rq, struct task_struct *task,
			      int oldprio);

	/* get_rr_interval removed - callback never called */

	void (*update_curr)(struct rq *rq);
};

static inline void put_prev_task(struct rq *rq, struct task_struct *prev)
{
	WARN_ON_ONCE(rq->curr != prev);
	prev->sched_class->put_prev_task(rq, prev);
}

static inline void set_next_task(struct rq *rq, struct task_struct *next)
{
	next->sched_class->set_next_task(rq, next, false);
}


 
#define DEFINE_SCHED_CLASS(name) \
const struct sched_class name##_sched_class \
	__aligned(__alignof__(struct sched_class)) \
	__section("__" #name "_sched_class")

 
extern struct sched_class __sched_class_highest[];
extern struct sched_class __sched_class_lowest[];

#define for_class_range(class, _from, _to) \
	for (class = (_from); class < (_to); class++)

#define for_each_class(class) \
	for_class_range(class, __sched_class_highest, __sched_class_lowest)

#define sched_class_above(_a, _b)	((_a) < (_b))

extern const struct sched_class dl_sched_class;
extern const struct sched_class rt_sched_class;
extern const struct sched_class fair_sched_class;
extern const struct sched_class idle_sched_class;

static inline bool sched_fair_runnable(struct rq *rq)
{
	return rq->cfs.nr_running > 0;
}

extern struct task_struct *pick_next_task_fair(struct rq *rq, struct task_struct *prev, struct rq_flags *rf);
extern struct task_struct *pick_next_task_idle(struct rq *rq);

extern void schedule_idle(void);

/* sched_init_granularity, init_sched_fair_class removed - empty functions */

extern void reweight_task(struct task_struct *p, int prio);

extern void resched_curr(struct rq *rq);
extern void resched_cpu(int cpu);

/* def_rt_bandwidth, init_rt_bandwidth removed - unused */

/* init_dl_task_timer, init_dl_inactive_task_timer removed - empty stubs */
/* init_entity_runnable_average, post_init_entity_util_avg removed - never called */
/* sched_tick_offload_init, sched_update_tick_dependency removed - unused */

/* add_nr_running and sub_nr_running inlined at fair.c - single caller each */

extern void check_preempt_curr(struct rq *rq, struct task_struct *p, int flags);
/* hrtick_enabled_fair, arch_scale_freq_tick, arch_scale_freq_capacity removed - never called */

extern struct sched_entity *__pick_first_entity(struct cfs_rq *cfs_rq);
/* resched_latency_warn removed - empty stub, never called */
extern void init_cfs_rq(struct cfs_rq *cfs_rq);
/* init_rt_rq, init_dl_rq removed - all rt_rq/dl_rq fields removed */
/* cpufreq_update_util, uclamp_rq_util_with, perf_domain_span, membarrier_switch_mm removed - unused stubs */

/* swake_up_all_locked removed - unused */
extern void __prepare_to_swait(struct swait_queue_head *q, struct swait_queue *wait);


#endif  
