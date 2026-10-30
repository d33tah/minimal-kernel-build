#include <linux/mutex.h>
#include <linux/sched/debug.h>

/*
 * Single-task kernel: init is the only schedulable task besides idle, and
 * interrupt context never takes a mutex, so a mutex is never contended.
 * Only the owner word is kept: lock = cmpxchg 0 -> current, unlock =
 * cmpxchg current -> 0. There is no wait list and no slow path.
 */
void __mutex_init(struct mutex *lock, const char *name,
		  struct lock_class_key *key)
{
	atomic_long_set(&lock->owner, 0);
}

int __sched mutex_trylock(struct mutex *lock)
{
	unsigned long zero = 0UL;

	return atomic_long_try_cmpxchg_acquire(&lock->owner, &zero,
					       (unsigned long)current);
}

void __sched mutex_lock(struct mutex *lock)
{
	might_sleep();
	mutex_trylock(lock);
}

void __sched mutex_unlock(struct mutex *lock)
{
	unsigned long curr = (unsigned long)current;

	atomic_long_try_cmpxchg_release(&lock->owner, &curr, 0UL);
}

int __sched mutex_lock_interruptible(struct mutex *lock)
{
	mutex_lock(lock);
	return 0;
}

int __sched mutex_lock_killable(struct mutex *lock)
{
	mutex_lock(lock);
	return 0;
}
