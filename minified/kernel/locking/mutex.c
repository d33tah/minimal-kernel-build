#include <linux/mutex.h>
#include <linux/sched/signal.h>
#include <linux/sched/rt.h>
#include <linux/sched/wake_q.h>
#include <linux/sched/debug.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/debug_locks.h>

#include "mutex.h"

/* MUTEX_WARN_ON removed - was empty stub */

void __mutex_init(struct mutex *lock, const char *name,
		  struct lock_class_key *key)
{
	atomic_long_set(&lock->owner, 0);
	raw_spin_lock_init(&lock->wait_lock);
	INIT_LIST_HEAD(&lock->wait_list);
}

#define MUTEX_FLAG_WAITERS 0x01
#define MUTEX_FLAG_HANDOFF 0x02
#define MUTEX_FLAG_PICKUP 0x04

#define MUTEX_FLAGS 0x07

/* __mutex_owner and __owner_task inlined */

bool mutex_is_locked(struct mutex *lock)
{
	return (atomic_long_read(&lock->owner) & ~MUTEX_FLAGS) != 0;
}

static inline unsigned long __owner_flags(unsigned long owner)
{
	return owner & MUTEX_FLAGS;
}

static inline struct task_struct *__mutex_trylock_common(struct mutex *lock,
							 bool handoff)
{
	unsigned long owner, curr = (unsigned long)current;

	owner = atomic_long_read(&lock->owner);
	for (;;) {
		unsigned long flags = __owner_flags(owner);
		unsigned long task = owner & ~MUTEX_FLAGS;

		if (task) {
			if (flags & MUTEX_FLAG_PICKUP) {
				if (task != curr)
					break;
				flags &= ~MUTEX_FLAG_PICKUP;
			} else if (handoff) {
				if (flags & MUTEX_FLAG_HANDOFF)
					break;
				flags |= MUTEX_FLAG_HANDOFF;
			} else {
				break;
			}
		} else {
			task = curr;
		}

		if (atomic_long_try_cmpxchg_acquire(&lock->owner, &owner,
						    task | flags)) {
			if (task == curr)
				return NULL;
			break;
		}
	}

	return (struct task_struct *)(owner &
				      ~MUTEX_FLAGS); /* __owner_task inlined */
}

/* __mutex_trylock_or_handoff inlined - just calls !__mutex_trylock_common */

static inline bool __mutex_trylock(struct mutex *lock)
{
	return !__mutex_trylock_common(lock, false);
}

static __always_inline bool __mutex_trylock_fast(struct mutex *lock)
{
	unsigned long curr = (unsigned long)current;
	unsigned long zero = 0UL;

	if (atomic_long_try_cmpxchg_acquire(&lock->owner, &zero, curr))
		return true;

	return false;
}

/* __mutex_unlock_fast inlined into mutex_unlock */

static inline bool __mutex_waiter_is_first(struct mutex *lock,
					   struct mutex_waiter *waiter)
{
	return list_first_entry(&lock->wait_list, struct mutex_waiter, list) ==
	       waiter;
}

/* __mutex_add_waiter inlined into __mutex_lock_common */

static void __mutex_remove_waiter(struct mutex *lock,
				  struct mutex_waiter *waiter)
{
	list_del(&waiter->list);
	if (likely(list_empty(&lock->wait_list)))
		atomic_long_andnot(MUTEX_FLAGS, &lock->owner);
}

/* __mutex_handoff inlined into __mutex_unlock_slowpath */

static void __sched __mutex_lock_slowpath(struct mutex *lock);

void __sched mutex_lock(struct mutex *lock)
{
	if (!__mutex_trylock_fast(lock))
		__mutex_lock_slowpath(lock);
}

/* mutex_optimistic_spin removed - always returned false */

static noinline void __sched __mutex_unlock_slowpath(struct mutex *lock,
						     unsigned long ip);

void __sched mutex_unlock(struct mutex *lock)
{
	/* __mutex_unlock_fast inlined */
	unsigned long curr = (unsigned long)current;
	if (atomic_long_try_cmpxchg_release(&lock->owner, &curr, 0UL))
		return;
	__mutex_unlock_slowpath(lock, _RET_IP_);
}

static __always_inline int __sched __mutex_lock_common(
	struct mutex *lock, unsigned int state, unsigned int subclass,
	struct lockdep_map *nest_lock, unsigned long ip)
{
	struct mutex_waiter waiter;
	int ret;

	preempt_disable();

	/* mutex_optimistic_spin call removed - always returned false */
	if (__mutex_trylock(lock)) {
		preempt_enable();
		return 0;
	}

	raw_spin_lock(&lock->wait_lock);
	if (__mutex_trylock(lock))
		goto skip_wait;

	waiter.task = current;

	list_add_tail(&waiter.list, &lock->wait_list);
	if (__mutex_waiter_is_first(lock, &waiter))
		atomic_long_or(MUTEX_FLAG_WAITERS, &lock->owner);

	set_current_state(state);
	for (;;) {
		bool first;

		if (__mutex_trylock(lock))
			goto acquired;

		if (signal_pending_state(state, current)) {
			ret = -EINTR;
			goto err;
		}

		raw_spin_unlock(&lock->wait_lock);
		schedule_preempt_disabled();

		first = __mutex_waiter_is_first(lock, &waiter);

		set_current_state(state);
		if (!__mutex_trylock_common(lock, first))
			break;

		/* mutex_optimistic_spin block removed - always returned false */

		raw_spin_lock(&lock->wait_lock);
	}
	raw_spin_lock(&lock->wait_lock);
acquired:
	__set_current_state(TASK_RUNNING);

	__mutex_remove_waiter(lock, &waiter);

skip_wait:
	raw_spin_unlock(&lock->wait_lock);
	preempt_enable();
	return 0;

err:
	__set_current_state(TASK_RUNNING);
	__mutex_remove_waiter(lock, &waiter);
	raw_spin_unlock(&lock->wait_lock);
	preempt_enable();
	return ret;
}

static int __sched __mutex_lock(struct mutex *lock, unsigned int state,
				unsigned int subclass,
				struct lockdep_map *nest_lock, unsigned long ip)
{
	return __mutex_lock_common(lock, state, subclass, nest_lock, ip);
}

static noinline void __sched __mutex_unlock_slowpath(struct mutex *lock,
						     unsigned long ip)
{
	struct task_struct *next = NULL;
	DEFINE_WAKE_Q(wake_q);
	unsigned long owner;

	owner = atomic_long_read(&lock->owner);
	for (;;) {
		if (owner & MUTEX_FLAG_HANDOFF)
			break;

		if (atomic_long_try_cmpxchg_release(&lock->owner, &owner,
						    __owner_flags(owner))) {
			if (owner & MUTEX_FLAG_WAITERS)
				break;

			return;
		}
	}

	raw_spin_lock(&lock->wait_lock);
	if (!list_empty(&lock->wait_list)) {
		struct mutex_waiter *waiter = list_first_entry(
			&lock->wait_list, struct mutex_waiter, list);

		next = waiter->task;

		wake_q_add(&wake_q, next);
	}

	/* __mutex_handoff inlined */
	if (owner & MUTEX_FLAG_HANDOFF) {
		unsigned long howner = atomic_long_read(&lock->owner);
		for (;;) {
			unsigned long new = (howner & MUTEX_FLAG_WAITERS);
			new |= (unsigned long)next;
			if (next)
				new |= MUTEX_FLAG_PICKUP;
			if (atomic_long_try_cmpxchg_release(&lock->owner,
							    &howner, new))
				break;
		}
	}

	raw_spin_unlock(&lock->wait_lock);

	wake_up_q(&wake_q);
}

static noinline int __sched __mutex_lock_killable_slowpath(struct mutex *lock);

static noinline int __sched
__mutex_lock_interruptible_slowpath(struct mutex *lock);

int __sched mutex_lock_interruptible(struct mutex *lock)
{
	if (__mutex_trylock_fast(lock))
		return 0;

	return __mutex_lock_interruptible_slowpath(lock);
}

int __sched mutex_lock_killable(struct mutex *lock)
{
	if (__mutex_trylock_fast(lock))
		return 0;

	return __mutex_lock_killable_slowpath(lock);
}

static noinline void __sched __mutex_lock_slowpath(struct mutex *lock)
{
	__mutex_lock(lock, TASK_UNINTERRUPTIBLE, 0, NULL, _RET_IP_);
}

static noinline int __sched __mutex_lock_killable_slowpath(struct mutex *lock)
{
	return __mutex_lock(lock, TASK_KILLABLE, 0, NULL, _RET_IP_);
}

static noinline int __sched
__mutex_lock_interruptible_slowpath(struct mutex *lock)
{
	return __mutex_lock(lock, TASK_INTERRUPTIBLE, 0, NULL, _RET_IP_);
}

int __sched mutex_trylock(struct mutex *lock)
{
	bool locked;

	locked = __mutex_trylock(lock);
	return locked;
}
