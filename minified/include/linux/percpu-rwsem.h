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
#include <linux/lockdep.h>

struct percpu_rw_semaphore {
	struct rcu_sync		rss;
	unsigned int __percpu	*read_count;
	struct rcuwait		writer;
	wait_queue_head_t	waiters;
	atomic_t		block;
};

#endif
