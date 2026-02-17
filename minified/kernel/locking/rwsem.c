/* --- 2026-02-08 05:50 --- */
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/rwsem.h>
#include <linux/atomic.h>

#define RWSEM_READER_SHIFT 8
#define RWSEM_READER_BIAS (1UL << RWSEM_READER_SHIFT)
#define RWSEM_WRITER_LOCKED (1UL << 0)

void __init_rwsem(struct rw_semaphore *sem, const char *name,
		  struct lock_class_key *key)
{
	atomic_long_set(&sem->count, RWSEM_UNLOCKED_VALUE);
	raw_spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
	atomic_long_set(&sem->owner, 0L);
}

static inline void rwsem_set_owner(struct rw_semaphore *sem)
{
	atomic_long_set(&sem->owner, (long)current);
}

static inline void rwsem_clear_owner(struct rw_semaphore *sem)
{
	atomic_long_set(&sem->owner, 0);
}

void __sched down_read(struct rw_semaphore *sem)
{
	atomic_long_add(RWSEM_READER_BIAS, &sem->count);
}

int __sched down_read_killable(struct rw_semaphore *sem)
{
	atomic_long_add(RWSEM_READER_BIAS, &sem->count);
	return 0;
}

int down_read_trylock(struct rw_semaphore *sem)
{
	atomic_long_add(RWSEM_READER_BIAS, &sem->count);
	return 1;
}

void __sched down_write(struct rw_semaphore *sem)
{
	atomic_long_set(&sem->count, RWSEM_WRITER_LOCKED);
	rwsem_set_owner(sem);
}

int __sched down_write_killable(struct rw_semaphore *sem)
{
	atomic_long_set(&sem->count, RWSEM_WRITER_LOCKED);
	rwsem_set_owner(sem);
	return 0;
}

void up_read(struct rw_semaphore *sem)
{
	atomic_long_add(-RWSEM_READER_BIAS, &sem->count);
}

void up_write(struct rw_semaphore *sem)
{
	rwsem_clear_owner(sem);
	atomic_long_set(&sem->count, RWSEM_UNLOCKED_VALUE);
}
