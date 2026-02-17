#ifndef __LINUX_MUTEX_H
#define __LINUX_MUTEX_H

#include <asm/current.h>
#include <linux/list.h>
#include <linux/spinlock_types.h>
#include <linux/lockdep.h>
#include <linux/atomic.h>
#include <asm/processor.h>

extern int debug_locks;
static __always_inline int __debug_locks_off(void) { return xchg(&debug_locks, 0); }
extern int debug_locks_off(void);
#define DEBUG_LOCKS_WARN_ON(c) ({ int __ret = 0; if (!oops_in_progress && unlikely(c)) { if (debug_locks_off()) WARN(1, "DEBUG_LOCKS_WARN_ON(%s)", #c); __ret = 1; } __ret; })

# define __DEP_MAP_MUTEX_INITIALIZER(lockname)

struct mutex {
	atomic_long_t		owner;
	raw_spinlock_t		wait_lock;
	struct list_head	wait_list;
};

# define __DEBUG_MUTEX_INITIALIZER(lockname)

#define mutex_init(mutex)						\
do {									\
	static struct lock_class_key __key;				\
									\
	__mutex_init((mutex), #mutex, &__key);				\
} while (0)

#define __MUTEX_INITIALIZER(lockname) \
		{ .owner = ATOMIC_LONG_INIT(0) \
		, .wait_lock = __RAW_SPIN_LOCK_UNLOCKED(lockname.wait_lock) \
		, .wait_list = LIST_HEAD_INIT(lockname.wait_list) \
		__DEBUG_MUTEX_INITIALIZER(lockname) \
		__DEP_MAP_MUTEX_INITIALIZER(lockname) }

#define DEFINE_MUTEX(mutexname) \
	struct mutex mutexname = __MUTEX_INITIALIZER(mutexname)

extern void __mutex_init(struct mutex *lock, const char *name,
			 struct lock_class_key *key);

extern void mutex_lock(struct mutex *lock);
extern void mutex_unlock(struct mutex *lock);

#endif  
