
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>


#if BITS_PER_LONG == 64
# define LDSEM_ACTIVE_MASK	0xffffffffL
#else
# define LDSEM_ACTIVE_MASK	0x0000ffffL
#endif

#define LDSEM_UNLOCKED		0L
#define LDSEM_ACTIVE_BIAS	1L
#define LDSEM_WAIT_BIAS		(-LDSEM_ACTIVE_MASK-1)
#define LDSEM_READ_BIAS		LDSEM_ACTIVE_BIAS
#define LDSEM_WRITE_BIAS	(LDSEM_WAIT_BIAS + LDSEM_ACTIVE_BIAS)

void __init_ldsem(struct ld_semaphore *sem, const char *name,
		  struct lock_class_key *key)
{
	atomic_long_set(&sem->count, LDSEM_UNLOCKED);
	sem->wait_readers = 0;
	raw_spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->read_wait);
	INIT_LIST_HEAD(&sem->write_wait);
}

/*
 * Contention slow paths removed: this is a uniprocessor (SMP=n) non-preemptible
 * (PREEMPTION=n) kernel and the tty ldisc_sem is only taken serially around the
 * single console, so the read/write acquire fast paths never fail. The
 * down_read_failed / down_write_failed wait machinery, the ldsem_waiter queue,
 * and the ldsem_wake wakeup helpers were therefore unreachable and are gone.
 */

static int __ldsem_down_read_nested(struct ld_semaphore *sem,
					   int subclass, long timeout)
{
	rwsem_acquire_read(&sem->dep_map, subclass, 0, _RET_IP_);
	atomic_long_add_return(LDSEM_READ_BIAS, &sem->count);
	lock_acquired(&sem->dep_map, _RET_IP_);
	return 1;
}

static int __ldsem_down_write_nested(struct ld_semaphore *sem,
					    int subclass, long timeout)
{
	rwsem_acquire(&sem->dep_map, subclass, 0, _RET_IP_);
	atomic_long_add_return(LDSEM_WRITE_BIAS, &sem->count);
	lock_acquired(&sem->dep_map, _RET_IP_);
	return 1;
}


int __sched ldsem_down_read(struct ld_semaphore *sem, long timeout)
{
	might_sleep();
	return __ldsem_down_read_nested(sem, 0, timeout);
}

int ldsem_down_read_trylock(struct ld_semaphore *sem)
{
	long count = atomic_long_read(&sem->count);

	while (count >= 0) {
		if (atomic_long_try_cmpxchg(&sem->count, &count, count + LDSEM_READ_BIAS)) {
			rwsem_acquire_read(&sem->dep_map, 0, 1, _RET_IP_);
			lock_acquired(&sem->dep_map, _RET_IP_);
			return 1;
		}
	}
	return 0;
}

int __sched ldsem_down_write(struct ld_semaphore *sem, long timeout)
{
	might_sleep();
	return __ldsem_down_write_nested(sem, 0, timeout);
}

void ldsem_up_read(struct ld_semaphore *sem)
{
	rwsem_release(&sem->dep_map, _RET_IP_);
	atomic_long_add_return(-LDSEM_READ_BIAS, &sem->count);
}

void ldsem_up_write(struct ld_semaphore *sem)
{
	rwsem_release(&sem->dep_map, _RET_IP_);
	atomic_long_add_return(-LDSEM_WRITE_BIAS, &sem->count);
}


