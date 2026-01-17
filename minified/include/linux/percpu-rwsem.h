#ifndef _LINUX_PERCPU_RWSEM_H
#define _LINUX_PERCPU_RWSEM_H

#include <linux/atomic.h>
#include <linux/percpu.h>
#include <linux/rcuwait.h>
#include <linux/wait.h>
#include <linux/rcu_sync.h>
#include <linux/lockdep.h>

struct percpu_rw_semaphore {
	struct rcu_sync		rss;
	unsigned int __percpu	*read_count;
	struct rcuwait		writer;
	wait_queue_head_t	waiters;
	atomic_t		block;
};

extern bool __percpu_down_read(struct percpu_rw_semaphore *, bool);

static inline void percpu_down_read(struct percpu_rw_semaphore *sem)
{
	might_sleep();

	/* rwsem_acquire_read removed - empty stub */

	preempt_disable();
	 
	if (likely(rcu_sync_is_idle(&sem->rss)))
		this_cpu_inc(*sem->read_count);
	else
		__percpu_down_read(sem, false);  
	 
	preempt_enable();
}

/* percpu_down_read_trylock removed - never called */

static inline void percpu_up_read(struct percpu_rw_semaphore *sem)
{
	/* rwsem_release removed - empty stub */

	preempt_disable();
	 
	if (likely(rcu_sync_is_idle(&sem->rss))) {
		this_cpu_dec(*sem->read_count);
	} else {
		 
		smp_mb();  
		 
		this_cpu_dec(*sem->read_count);
		rcuwait_wake_up(&sem->writer);
	}
	preempt_enable();
}

/* percpu_down_write removed - never called */

extern int __percpu_init_rwsem(struct percpu_rw_semaphore *,
				const char *, struct lock_class_key *);

extern void percpu_free_rwsem(struct percpu_rw_semaphore *);

#define percpu_init_rwsem(sem)					\
({								\
	static struct lock_class_key rwsem_key;			\
	__percpu_init_rwsem(sem, #sem, &rwsem_key);		\
})

#endif
