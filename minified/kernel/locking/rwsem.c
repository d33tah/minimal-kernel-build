#include <linux/sched/debug.h>
#include <linux/rwsem.h>

/*
 * Single-task kernel: init is the only schedulable task besides idle, and
 * interrupt context never takes an rwsem, so an rwsem is never contended.
 * Only the count and owner words are kept (rwsem_is_locked() reads count);
 * there is no wait list, no waiter/handoff flags and no slow path. A
 * contended rwsem would need a second task to release it, so down_read()
 * of a write-locked rwsem and down_write() of a locked one BUG.
 */
#define RWSEM_READER_OWNED (1UL << 0)

#define RWSEM_WRITER_LOCKED (1UL << 0)
#define RWSEM_FLAG_READFAIL (1UL << (BITS_PER_LONG - 1))

#define RWSEM_READER_SHIFT 8
#define RWSEM_READER_BIAS (1UL << RWSEM_READER_SHIFT)
#define RWSEM_READ_FAILED_MASK (RWSEM_WRITER_LOCKED | RWSEM_FLAG_READFAIL)

static inline void rwsem_set_reader_owned(struct rw_semaphore *sem)
{
	atomic_long_set(&sem->owner,
			(unsigned long)current | RWSEM_READER_OWNED);
}

void __init_rwsem(struct rw_semaphore *sem, const char *name,
		  struct lock_class_key *key)
{
	atomic_long_set(&sem->count, RWSEM_UNLOCKED_VALUE);
	atomic_long_set(&sem->owner, 0L);
}

void __sched down_read(struct rw_semaphore *sem)
{
	long count;

	count = atomic_long_add_return_acquire(RWSEM_READER_BIAS, &sem->count);
	BUG_ON(count & RWSEM_READ_FAILED_MASK);
	rwsem_set_reader_owned(sem);
}

int __sched down_read_killable(struct rw_semaphore *sem)
{
	down_read(sem);
	return 0;
}

int down_read_trylock(struct rw_semaphore *sem)
{
	long tmp = atomic_long_read(&sem->count);

	while (!(tmp & RWSEM_READ_FAILED_MASK)) {
		if (atomic_long_try_cmpxchg_acquire(&sem->count, &tmp,
						    tmp + RWSEM_READER_BIAS)) {
			rwsem_set_reader_owned(sem);
			return 1;
		}
	}
	return 0;
}

void __sched down_write(struct rw_semaphore *sem)
{
	long tmp = RWSEM_UNLOCKED_VALUE;

	BUG_ON(!atomic_long_try_cmpxchg_acquire(&sem->count, &tmp,
						RWSEM_WRITER_LOCKED));
	atomic_long_set(&sem->owner, (long)current);
}

int __sched down_write_killable(struct rw_semaphore *sem)
{
	down_write(sem);
	return 0;
}

void up_read(struct rw_semaphore *sem)
{
	atomic_long_add_return_release(-RWSEM_READER_BIAS, &sem->count);
}

void up_write(struct rw_semaphore *sem)
{
	atomic_long_set(&sem->owner, 0);
	atomic_long_fetch_add_release(-RWSEM_WRITER_LOCKED, &sem->count);
}
