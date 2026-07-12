
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/rwsem.h>

#define LOCK_EVENT(name)	LOCKEVENT_ ## name,
enum lock_events { LOCK_EVENT(rwsem_sleep_reader) LOCK_EVENT(rwsem_sleep_writer) LOCK_EVENT(rwsem_wake_reader) LOCK_EVENT(rwsem_wake_writer) LOCK_EVENT(rwsem_opt_lock) LOCK_EVENT(rwsem_opt_fail) LOCK_EVENT(rwsem_opt_nospin) LOCK_EVENT(rwsem_rlock) LOCK_EVENT(rwsem_rlock_steal) LOCK_EVENT(rwsem_rlock_fast) LOCK_EVENT(rwsem_rlock_fail) LOCK_EVENT(rwsem_rlock_handoff) LOCK_EVENT(rwsem_wlock) LOCK_EVENT(rwsem_wlock_fail) LOCK_EVENT(rwsem_wlock_handoff) lockevent_num, LOCKEVENT_reset_cnts = lockevent_num, };
#undef LOCK_EVENT
/* end lock_events headers */

#define RWSEM_READER_OWNED	(1UL << 0)

# define DEBUG_RWSEMS_WARN_ON(c, sem)

#define RWSEM_WRITER_LOCKED	(1UL << 0)
#define RWSEM_FLAG_WAITERS	(1UL << 1)
#define RWSEM_FLAG_HANDOFF	(1UL << 2)
#define RWSEM_FLAG_READFAIL	(1UL << (BITS_PER_LONG - 1))

#define RWSEM_READER_SHIFT	8
#define RWSEM_READER_BIAS	(1UL << RWSEM_READER_SHIFT)
#define RWSEM_READER_MASK	(~(RWSEM_READER_BIAS - 1))
#define RWSEM_WRITER_MASK	RWSEM_WRITER_LOCKED
#define RWSEM_LOCK_MASK		(RWSEM_WRITER_MASK|RWSEM_READER_MASK)
#define RWSEM_READ_FAILED_MASK	(RWSEM_WRITER_MASK|RWSEM_FLAG_WAITERS|				 RWSEM_FLAG_HANDOFF|RWSEM_FLAG_READFAIL)

static inline void rwsem_set_owner(struct rw_semaphore *sem)
{
	atomic_long_set(&sem->owner, (long)current);
}

static inline void rwsem_clear_owner(struct rw_semaphore *sem)
{
	atomic_long_set(&sem->owner, 0);
}

static inline void __rwsem_set_reader_owned(struct rw_semaphore *sem, struct task_struct *owner)
{
	unsigned long val = (unsigned long)owner | RWSEM_READER_OWNED;

	atomic_long_set(&sem->owner, val);
}

static inline void rwsem_set_reader_owned(struct rw_semaphore *sem)
{
	__rwsem_set_reader_owned(sem, current);
}

static inline bool rwsem_read_trylock(struct rw_semaphore *sem, long *cntp)
{
	*cntp = atomic_long_add_return_acquire(RWSEM_READER_BIAS, &sem->count);

	WARN_ON_ONCE(*cntp < 0);

	if (!(*cntp & RWSEM_READ_FAILED_MASK)) {
		rwsem_set_reader_owned(sem);
		return true;
	}

	return false;
}

static inline bool rwsem_write_trylock(struct rw_semaphore *sem)
{
	long tmp = RWSEM_UNLOCKED_VALUE;

	if (atomic_long_try_cmpxchg_acquire(&sem->count, &tmp, RWSEM_WRITER_LOCKED)) {
		rwsem_set_owner(sem);
		return true;
	}

	return false;
}

void __init_rwsem(struct rw_semaphore *sem, const char *name, struct lock_class_key *key)
{
	atomic_long_set(&sem->count, RWSEM_UNLOCKED_VALUE);
	raw_spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
	atomic_long_set(&sem->owner, 0L);
}

static struct rw_semaphore __sched *
rwsem_down_read_slowpath(struct rw_semaphore *sem, long count, unsigned int state)
{

	rwsem_set_reader_owned(sem);
	return sem;
}

static struct rw_semaphore *rwsem_wake(struct rw_semaphore *sem)
{

	return sem;
}

static inline int __down_read_common(struct rw_semaphore *sem, int state)
{
	long count;

	if (!rwsem_read_trylock(sem, &count)) {
		if (IS_ERR(rwsem_down_read_slowpath(sem, count, state)))
			return -EINTR;
	}
	return 0;
}

static inline void __down_read(struct rw_semaphore *sem)
{
	__down_read_common(sem, TASK_UNINTERRUPTIBLE);
}


static inline int __down_read_killable(struct rw_semaphore *sem)
{
	return __down_read_common(sem, TASK_KILLABLE);
}

static inline int __down_read_trylock(struct rw_semaphore *sem)
{
	long tmp;

	DEBUG_RWSEMS_WARN_ON(sem->magic != sem, sem);

	tmp = atomic_long_read(&sem->count);
	while (!(tmp & RWSEM_READ_FAILED_MASK)) {
		if (atomic_long_try_cmpxchg_acquire(&sem->count, &tmp, tmp + RWSEM_READER_BIAS)) {
			rwsem_set_reader_owned(sem);
			return 1;
		}
	}
	return 0;
}

static inline int __down_write_common(struct rw_semaphore *sem, int state)
{
	rwsem_write_trylock(sem);
	return 0;
}

static inline void __down_write(struct rw_semaphore *sem)
{
	__down_write_common(sem, TASK_UNINTERRUPTIBLE);
}

static inline int __down_write_killable(struct rw_semaphore *sem)
{
	return __down_write_common(sem, TASK_KILLABLE);
}

static inline int __down_write_trylock(struct rw_semaphore *sem)
{
	DEBUG_RWSEMS_WARN_ON(sem->magic != sem, sem);
	return rwsem_write_trylock(sem);
}

static inline void __up_read(struct rw_semaphore *sem)
{
	long tmp;

	DEBUG_RWSEMS_WARN_ON(sem->magic != sem, sem);

	tmp = atomic_long_add_return_release(-RWSEM_READER_BIAS, &sem->count);
	DEBUG_RWSEMS_WARN_ON(tmp < 0, sem);
	if (unlikely((tmp & (RWSEM_LOCK_MASK|RWSEM_FLAG_WAITERS)) == RWSEM_FLAG_WAITERS)) {
		rwsem_wake(sem);
	}
}

static inline void __up_write(struct rw_semaphore *sem)
{
	long tmp;

	DEBUG_RWSEMS_WARN_ON(sem->magic != sem, sem);

	rwsem_clear_owner(sem);
	tmp = atomic_long_fetch_add_release(-RWSEM_WRITER_LOCKED, &sem->count);
	if (unlikely(tmp & RWSEM_FLAG_WAITERS))
		rwsem_wake(sem);
}


void __sched down_read(struct rw_semaphore *sem)
{
	might_sleep();
	rwsem_acquire_read(&sem->dep_map, 0, 0, _RET_IP_);

	LOCK_CONTENDED(sem, __down_read_trylock, __down_read);
}


int __sched down_read_killable(struct rw_semaphore *sem)
{
	might_sleep();
	rwsem_acquire_read(&sem->dep_map, 0, 0, _RET_IP_);

	if (LOCK_CONTENDED_RETURN(sem, __down_read_trylock, __down_read_killable)) {
		rwsem_release(&sem->dep_map, _RET_IP_);
		return -EINTR;
	}

	return 0;
}

int down_read_trylock(struct rw_semaphore *sem)
{
	int ret = __down_read_trylock(sem);

	if (ret == 1)
		rwsem_acquire_read(&sem->dep_map, 0, 1, _RET_IP_);
	return ret;
}

void __sched down_write(struct rw_semaphore *sem)
{
	might_sleep();
	rwsem_acquire(&sem->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(sem, __down_write_trylock, __down_write);
}

int __sched down_write_killable(struct rw_semaphore *sem)
{
	might_sleep();
	rwsem_acquire(&sem->dep_map, 0, 0, _RET_IP_);

	if (LOCK_CONTENDED_RETURN(sem, __down_write_trylock, __down_write_killable)) {
		rwsem_release(&sem->dep_map, _RET_IP_);
		return -EINTR;
	}

	return 0;
}

void up_read(struct rw_semaphore *sem)
{
	rwsem_release(&sem->dep_map, _RET_IP_);
	__up_read(sem);
}

void up_write(struct rw_semaphore *sem)
{
	rwsem_release(&sem->dep_map, _RET_IP_);
	__up_write(sem);
}

