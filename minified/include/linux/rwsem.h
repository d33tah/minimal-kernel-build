
#ifndef _LINUX_RWSEM_H
#define _LINUX_RWSEM_H

#include <linux/linkage.h>

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/err.h>

# define __RWSEM_DEP_MAP_INIT(lockname)



struct rw_semaphore {
	atomic_long_t count;
	 
	atomic_long_t owner;
	raw_spinlock_t wait_lock;
	struct list_head wait_list;
};

static inline int rwsem_is_locked(struct rw_semaphore *sem)
{
	return atomic_long_read(&sem->count) != 0;
}

#define RWSEM_UNLOCKED_VALUE		0L
#define __RWSEM_COUNT_INIT(name)	.count = ATOMIC_LONG_INIT(RWSEM_UNLOCKED_VALUE)


# define __RWSEM_DEBUG_INIT(lockname)

#define __RWSEM_OPT_INIT(lockname)

#define __RWSEM_INITIALIZER(name)				\
	{ __RWSEM_COUNT_INIT(name),				\
	  .owner = ATOMIC_LONG_INIT(0),				\
	  __RWSEM_OPT_INIT(name)				\
	  .wait_lock = __RAW_SPIN_LOCK_UNLOCKED(name.wait_lock),\
	  .wait_list = LIST_HEAD_INIT((name).wait_list),	\
	  __RWSEM_DEBUG_INIT(name)				\
	  __RWSEM_DEP_MAP_INIT(name) }

#define DECLARE_RWSEM(name) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name)

extern void __init_rwsem(struct rw_semaphore *sem, const char *name,
			 struct lock_class_key *key);

#define init_rwsem(sem)						\
do {								\
	static struct lock_class_key __key;			\
								\
	__init_rwsem((sem), #sem, &__key);			\
} while (0)

extern void down_read(struct rw_semaphore *sem);
extern int __must_check down_read_interruptible(struct rw_semaphore *sem);
extern int __must_check down_read_killable(struct rw_semaphore *sem);

extern int down_read_trylock(struct rw_semaphore *sem);

extern void down_write(struct rw_semaphore *sem);
extern int __must_check down_write_killable(struct rw_semaphore *sem);

extern int down_write_trylock(struct rw_semaphore *sem);

extern void up_read(struct rw_semaphore *sem);

extern void up_write(struct rw_semaphore *sem);

extern void downgrade_write(struct rw_semaphore *sem);

# define down_read_nested(sem, subclass)		down_read(sem)
# define down_read_killable_nested(sem, subclass)	down_read_killable(sem)
# define down_write_nest_lock(sem, nest_lock)	down_write(sem)
# define down_write_nested(sem, subclass)	down_write(sem)
# define down_write_killable_nested(sem, subclass)	down_write_killable(sem)
# define down_read_non_owner(sem)		down_read(sem)
# define up_read_non_owner(sem)			up_read(sem)

#endif  
