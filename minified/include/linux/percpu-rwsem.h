#ifndef _LINUX_PERCPU_RWSEM_H
#define _LINUX_PERCPU_RWSEM_H

#include <linux/atomic.h>
#include <linux/percpu.h>
#include <linux/sched/signal.h>
struct rcuwait { struct task_struct __rcu *task; };
#include <linux/wait.h>
#include <linux/rcupdate.h>
struct rcu_sync {
	int gp_state;
	wait_queue_head_t gp_wait;
};
extern void rcu_sync_init(struct rcu_sync *);
extern void rcu_sync_dtor(struct rcu_sync *);
#include <linux/lockdep.h>

struct percpu_rw_semaphore {
	struct rcu_sync		rss;
	unsigned int __percpu	*read_count;
	struct rcuwait		writer;
	wait_queue_head_t	waiters;
	atomic_t		block;
};

extern int __percpu_init_rwsem(struct percpu_rw_semaphore *,
				const char *, struct lock_class_key *);

extern void percpu_free_rwsem(struct percpu_rw_semaphore *);

#endif
