#ifndef __LINUX_MUTEX_H
#define __LINUX_MUTEX_H

#include <linux/spinlock_types_raw.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <asm/processor.h>
#include <linux/atomic.h>

struct mutex {
	atomic_long_t owner;
};

#define mutex_init(mutex)                              \
	do {                                           \
		static struct lock_class_key __key;    \
		__mutex_init((mutex), #mutex, &__key); \
	} while (0)

#define __MUTEX_INITIALIZER(lockname) { .owner = ATOMIC_LONG_INIT(0) }

#define DEFINE_MUTEX(mutexname) \
	struct mutex mutexname = __MUTEX_INITIALIZER(mutexname)

extern void __mutex_init(struct mutex *lock, const char *name,
			 struct lock_class_key *key);

extern void mutex_lock(struct mutex *lock);
extern int __must_check mutex_lock_interruptible(struct mutex *lock);
extern void mutex_unlock(struct mutex *lock);

#endif
