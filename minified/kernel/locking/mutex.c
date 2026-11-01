// SPDX-License-Identifier: GPL-2.0-only
/*
 * kernel/locking/mutex.c
 *
 * Mutexes: blocking mutual exclusion locks
 *
 * Started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * Many thanks to Arjan van de Ven, Thomas Gleixner, Steven Rostedt and
 * David Howells for suggestions and improvements.
 *
 *  - Adaptive spinning for mutexes by Peter Zijlstra. (Ported to mainline
 *    from the -rt tree, where it was originally implemented for rtmutexes
 *    by Steven Rostedt, based on work by Gregory Haskins, Peter Morreale
 *    and Sven Dietrich.
 *
 * Also see Documentation/locking/mutex-design.rst.
 */
#include <linux/mutex.h>
#include <linux/sched/debug.h>
#include <linux/spinlock.h>

/*
 * Single-task kernel: init is the only schedulable task besides idle, and
 * interrupt context never takes a mutex, so a mutex is never contended.
 * Only the owner word is kept: lock = cmpxchg 0 -> current, unlock =
 * cmpxchg current -> 0. There is no wait list and no slow path: upstream's
 * slow path sleeps until the owner unlocks, and with one task no other owner
 * can exist, so a mutex found locked is a bug (recursion or a lock taken
 * from interrupt context) and mutex_lock() BUGs instead of proceeding.
 */
void __mutex_init(struct mutex *lock, const char *name,
		  struct lock_class_key *key)
{
	atomic_long_set(&lock->owner, 0);
}

static int __sched mutex_trylock(struct mutex *lock)
{
	unsigned long zero = 0UL;

	return atomic_long_try_cmpxchg_acquire(&lock->owner, &zero,
					       (unsigned long)current);
}

void __sched mutex_lock(struct mutex *lock)
{
	BUG_ON(!mutex_trylock(lock));
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
