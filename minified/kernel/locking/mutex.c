/* --- 2026-02-08 05:55 --- */
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/sched/debug.h>
#include <linux/atomic.h>

void __mutex_init(struct mutex *lock, const char *name,
		  struct lock_class_key *key)
{
	atomic_long_set(&lock->owner, 0);
	raw_spin_lock_init(&lock->wait_lock);
	INIT_LIST_HEAD(&lock->wait_list);
}

void __sched mutex_lock(struct mutex *lock)
{
	atomic_long_set(&lock->owner, (unsigned long)current);
}

void __sched mutex_unlock(struct mutex *lock)
{
	atomic_long_set(&lock->owner, 0UL);
}
