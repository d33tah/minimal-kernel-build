
#include <linux/mutex.h>
#include <linux/sched/signal.h>
#include <linux/atomic.h>
struct semaphore {
	raw_spinlock_t lock;
	unsigned int count;
	struct list_head wait_list;
};
#define __SEMAPHORE_INITIALIZER(name, n)                       \
	{                                                      \
		.lock = __RAW_SPIN_LOCK_UNLOCKED((name).lock), \
		.count = n,                                    \
		.wait_list = LIST_HEAD_INIT((name).wait_list), \
	}

/* Simplified for single-task kernel: no contention possible */
void down(struct semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->lock, flags);
	sem->count--;
	raw_spin_unlock_irqrestore(&sem->lock, flags);
}

void up(struct semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->lock, flags);
	sem->count++;
	raw_spin_unlock_irqrestore(&sem->lock, flags);
}

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
