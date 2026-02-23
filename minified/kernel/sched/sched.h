 
#ifndef _KERNEL_SCHED_SCHED_H
#define _KERNEL_SCHED_SCHED_H

#include <linux/sched.h>
#include <linux/sched/mm.h>

#include <linux/sched/signal.h>
/* end sched/stat.h */

#include <linux/sched/task.h>

#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/bug.h>

#include <linux/cgroup.h>
#include <linux/ctype.h>
#include <linux/file.h>

#include <linux/interrupt.h>
#include <linux/jiffies.h>

#include <linux/kthread.h>

#include <linux/lockdep.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <linux/compiler.h>
/* ktime.h inlined */
typedef s64 ktime_t;
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include <linux/rcupdate.h>
#include <linux/seqlock.h>

#include <linux/jump_label.h>
#include <linux/cpu.h>

#include <linux/syscalls.h>
#include <linux/topology.h>
#include <linux/types.h>

#include <linux/uaccess.h>

/* wait_bit.h - now inlined in fs.h, included above */

# define SCHED_WARN_ON(x)      ({ (void)(x), 0; })

struct rq;

#define TASK_ON_RQ_QUEUED	1
# define scale_load(w)		(w)

struct cfs_rq {
	struct load_weight	load;
	unsigned int		nr_running;
	unsigned int		h_nr_running;
	u64			min_vruntime;
	struct rb_root_cached	tasks_timeline;

	struct sched_entity	*curr;

};

struct rt_rq {
	char dummy; /* Empty struct not allowed in C */
};

struct dl_rq {
	char dummy; /* Empty struct not allowed in C */
};

struct rq {
	 
	raw_spinlock_t		__lock;

	unsigned int		nr_running;

	struct cfs_rq		cfs;
	struct rt_rq		rt;
	struct dl_rq		dl;

	struct task_struct __rcu	*curr;
	struct task_struct	*idle;
	struct task_struct	*stop;
	struct mm_struct	*prev_mm;

	unsigned int		clock_update_flags;
	u64			clock;
	u64			clock_task ____cacheline_aligned;

};

static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return container_of(cfs_rq, struct rq, cfs);
}

static inline raw_spinlock_t *rq_lockp(struct rq *rq)
{
	return &rq->__lock;
}

extern void raw_spin_rq_lock_nested(struct rq *rq, int subclass);
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
DECLARE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);

#define cpu_rq(cpu)		(&per_cpu(runqueues, (cpu)))
#define this_rq()		this_cpu_ptr(&runqueues)
#define task_rq(p)		cpu_rq(task_cpu(p))

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

extern void update_rq_clock(struct rq *rq);

#define RQCF_REQ_SKIP		0x01
#define RQCF_ACT_SKIP		0x02

static inline void assert_clock_updated(struct rq *rq)
{

	SCHED_WARN_ON(rq->clock_update_flags < RQCF_ACT_SKIP);
}

static inline u64 rq_clock_task(struct rq *rq)
{
	assert_clock_updated(rq);

	return rq->clock_task;
}

static inline void rq_clock_skip_update(struct rq *rq)
{
	rq->clock_update_flags |= RQCF_REQ_SKIP;
}

struct rq_flags {
	unsigned long flags;
};

#define rq_pin_lock(rq, rf) do { } while (0)
#define rq_unpin_lock(rq, rf) do { } while (0)

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
rq_lock(struct rq *rq, struct rq_flags *rf)
	__acquires(rq->lock)
{
	raw_spin_rq_lock(rq);
	rq_pin_lock(rq, rf);
}

static inline void
rq_unlock(struct rq *rq, struct rq_flags *rf)
	__releases(rq->lock)
{
	rq_unpin_lock(rq, rf);
	raw_spin_rq_unlock(rq);
}

static inline void __set_task_cpu(struct task_struct *p, unsigned int cpu)
{
}

static inline int task_on_rq_queued(struct task_struct *p)
{
	return p->on_rq == TASK_ON_RQ_QUEUED;
}

#define WF_FORK     0x04

extern const int		sched_prio_to_weight[40];

#define DEQUEUE_SLEEP		0x01
#define DEQUEUE_SAVE		0x02  
#define DEQUEUE_MOVE		0x04  
#define DEQUEUE_NOCLOCK		0x08  

#define ENQUEUE_WAKEUP		0x01
#define ENQUEUE_NOCLOCK		0x08

struct sched_class {

	void (*enqueue_task) (struct rq *rq, struct task_struct *p, int flags);
	void (*dequeue_task) (struct rq *rq, struct task_struct *p, int flags);

	void (*check_preempt_curr)(struct rq *rq, struct task_struct *p, int flags);

	void (*put_prev_task)(struct rq *rq, struct task_struct *p);

	void (*task_fork)(struct task_struct *p);
};

static inline void put_prev_task(struct rq *rq, struct task_struct *prev)
{
	WARN_ON_ONCE(rq->curr != prev);
	prev->sched_class->put_prev_task(rq, prev);
}

#define DEFINE_SCHED_CLASS(name) \
const struct sched_class name##_sched_class \
	__aligned(__alignof__(struct sched_class)) \
	__section("__" #name "_sched_class")

#define sched_class_above(_a, _b)	((_a) < (_b))

extern const struct sched_class fair_sched_class;
extern const struct sched_class idle_sched_class;

static inline bool sched_fair_runnable(struct rq *rq)
{
	return rq->cfs.nr_running > 0;
}

extern struct task_struct *pick_next_task_fair(struct rq *rq, struct task_struct *prev, struct rq_flags *rf);
extern struct task_struct *pick_next_task_idle(struct rq *rq);

extern void schedule_idle(void);

extern void reweight_task(struct task_struct *p, int prio);

extern void resched_curr(struct rq *rq);
extern void resched_cpu(int cpu);

extern void check_preempt_curr(struct rq *rq, struct task_struct *p, int flags);

extern void init_cfs_rq(struct cfs_rq *cfs_rq);

#endif  
