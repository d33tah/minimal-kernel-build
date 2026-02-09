

#include <linux/compiler.h>
#include <linux/kernel.h>
/* linux/export.h removed - no EXPORT_SYMBOL */
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/signal.h>
/* Inlined from linux/semaphore.h */
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
#define DEFINE_SEMAPHORE(name) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)
extern void down(struct semaphore *sem);
extern int __must_check down_trylock(struct semaphore *sem);
extern void up(struct semaphore *sem);
#include <linux/spinlock.h>

static inline int __sched ___down_common(struct semaphore *sem, long state,
					 long timeout);

/* __down inlined into down (~3 LOC) */
void down(struct semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->lock, flags);
	if (likely(sem->count > 0))
		sem->count--;
	else
		___down_common(sem, TASK_UNINTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
	raw_spin_unlock_irqrestore(&sem->lock, flags);
}

int down_trylock(struct semaphore *sem)
{
	unsigned long flags;
	int count;

	raw_spin_lock_irqsave(&sem->lock, flags);
	count = sem->count - 1;
	if (likely(count >= 0))
		sem->count = count;
	raw_spin_unlock_irqrestore(&sem->lock, flags);

	return (count < 0);
}

struct semaphore_waiter {
	struct list_head list;
	struct task_struct *task;
	bool up;
};

/* __up inlined into up (~3 LOC) */
void up(struct semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->lock, flags);
	if (likely(list_empty(&sem->wait_list)))
		sem->count++;
	else {
		struct semaphore_waiter *waiter = list_first_entry(
			&sem->wait_list, struct semaphore_waiter, list);
		list_del(&waiter->list);
		waiter->up = true;
		wake_up_process(waiter->task);
	}
	raw_spin_unlock_irqrestore(&sem->lock, flags);
}

static inline int __sched ___down_common(struct semaphore *sem, long state,
					 long timeout)
{
	struct semaphore_waiter waiter;

	list_add_tail(&waiter.list, &sem->wait_list);
	waiter.task = current;
	waiter.up = false;

	for (;;) {
		if (signal_pending_state(state, current))
			goto interrupted;
		if (unlikely(timeout <= 0))
			goto timed_out;
		__set_current_state(state);
		raw_spin_unlock_irq(&sem->lock);
		timeout = schedule_timeout(timeout);
		raw_spin_lock_irq(&sem->lock);
		if (waiter.up)
			return 0;
	}

timed_out:
	list_del(&waiter.list);
	return -ETIME;

interrupted:
	list_del(&waiter.list);
	return -EINTR;
}
