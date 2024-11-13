/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RT Mutexes: blocking mutual exclusion locks with PI support
 *
 * started by Ingo Molnar and Thomas Gleixner:
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 * This file contains the public data structure and API definitions.
 */

#ifndef __LINUX_RT_MUTEX_H
#define __LINUX_RT_MUTEX_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/rbtree_types.h>
#include <linux/spinlock_types_raw.h>

extern int max_lock_depth; /* for sysctl */

struct rt_mutex_base {
	raw_spinlock_t		wait_lock;
	struct rb_root_cached   waiters;
	struct task_struct	*owner;
};

#define __RT_MUTEX_BASE_INITIALIZER(rtbasename)				\
{									\
	.wait_lock = __RAW_SPIN_LOCK_UNLOCKED(rtbasename.wait_lock),	\
	.waiters = RB_ROOT_CACHED,					\
	.owner = NULL							\
}

/**
 * rt_mutex_base_is_locked - is the rtmutex locked
 * @lock: the mutex to be queried
 *
 * Returns true if the mutex is locked, false if unlocked.
 */
static inline bool rt_mutex_base_is_locked(struct rt_mutex_base *lock)
{
	return READ_ONCE(lock->owner) != NULL;
}

extern void rt_mutex_base_init(struct rt_mutex_base *rtb);

/**
 * The rt_mutex structure
 *
 * @wait_lock:	spinlock to protect the structure
 * @waiters:	rbtree root to enqueue waiters in priority order;
 *              caches top-waiter (leftmost node).
 * @owner:	the mutex owner
 */
struct rt_mutex {
	struct rt_mutex_base	rtmutex;
};

struct rt_mutex_waiter;
struct hrtimer_sleeper;

static inline void rt_mutex_debug_task_free(struct task_struct *tsk) { }

#define rt_mutex_init(mutex) \
do { \
	static struct lock_class_key __key; \
	__rt_mutex_init(mutex, __func__, &__key); \
} while (0)

#define __DEP_MAP_RT_MUTEX_INITIALIZER(mutexname)

#define __RT_MUTEX_INITIALIZER(mutexname)				\
{									\
	.rtmutex = __RT_MUTEX_BASE_INITIALIZER(mutexname.rtmutex),	\
	__DEP_MAP_RT_MUTEX_INITIALIZER(mutexname)			\
}

#define DEFINE_RT_MUTEX(mutexname) \
	struct rt_mutex mutexname = __RT_MUTEX_INITIALIZER(mutexname)

extern void __rt_mutex_init(struct rt_mutex *lock, const char *name, struct lock_class_key *key);

extern void rt_mutex_lock(struct rt_mutex *lock);
#define rt_mutex_lock_nested(lock, subclass) rt_mutex_lock(lock)
#define rt_mutex_lock_nest_lock(lock, nest_lock) rt_mutex_lock(lock)

extern int rt_mutex_lock_interruptible(struct rt_mutex *lock);
extern int rt_mutex_lock_killable(struct rt_mutex *lock);
extern int rt_mutex_trylock(struct rt_mutex *lock);

extern void rt_mutex_unlock(struct rt_mutex *lock);

#endif
