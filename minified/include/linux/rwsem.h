
#ifndef _LINUX_RWSEM_H
#define _LINUX_RWSEM_H

#include <linux/spinlock.h>

struct rw_semaphore {
	atomic_long_t count;
	atomic_long_t owner;
};

static inline int rwsem_is_locked(struct rw_semaphore *sem)
{
	return atomic_long_read(&sem->count) != 0;
}

#define RWSEM_UNLOCKED_VALUE 0L
#define __RWSEM_INITIALIZER(name)                          \
	{ .count = ATOMIC_LONG_INIT(RWSEM_UNLOCKED_VALUE), \
	  .owner = ATOMIC_LONG_INIT(0) }

extern void __init_rwsem(struct rw_semaphore *sem, const char *name,
			 struct lock_class_key *key);

#define init_rwsem(sem)                             \
	do {                                        \
		static struct lock_class_key __key; \
		__init_rwsem((sem), #sem, &__key);  \
	} while (0)

extern void down_read(struct rw_semaphore *sem);
extern int __must_check down_read_killable(struct rw_semaphore *sem);

extern int down_read_trylock(struct rw_semaphore *sem);

extern void down_write(struct rw_semaphore *sem);
extern int __must_check down_write_killable(struct rw_semaphore *sem);

extern void up_read(struct rw_semaphore *sem);

extern void up_write(struct rw_semaphore *sem);

#define down_write_nested(sem, subclass) down_write(sem)

#endif
