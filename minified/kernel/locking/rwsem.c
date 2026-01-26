
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/sched/task.h>
#include <linux/sched/debug.h>
#include <linux/sched/wake_q.h>
#include <linux/sched/signal.h>
#include <linux/sched/clock.h>
#include <linux/export.h>
#include <linux/rwsem.h>
#include <linux/atomic.h>

/* lockevent enum and macros removed - were empty stubs */

#define RWSEM_READER_OWNED (1UL << 0)
#define RWSEM_NONSPINNABLE (1UL << 1)
#define RWSEM_OWNER_FLAGS_MASK (RWSEM_READER_OWNED | RWSEM_NONSPINNABLE)

/* DEBUG_RWSEMS_WARN_ON removed - was empty stub */

#define RWSEM_WRITER_LOCKED (1UL << 0)
#define RWSEM_FLAG_WAITERS (1UL << 1)
#define RWSEM_FLAG_HANDOFF (1UL << 2)
#define RWSEM_FLAG_READFAIL (1UL << (BITS_PER_LONG - 1))

#define RWSEM_READER_SHIFT 8
#define RWSEM_READER_BIAS (1UL << RWSEM_READER_SHIFT)
#define RWSEM_READER_MASK (~(RWSEM_READER_BIAS - 1))
#define RWSEM_WRITER_MASK RWSEM_WRITER_LOCKED
#define RWSEM_LOCK_MASK (RWSEM_WRITER_MASK | RWSEM_READER_MASK)
#define RWSEM_READ_FAILED_MASK                                         \
	(RWSEM_WRITER_MASK | RWSEM_FLAG_WAITERS | RWSEM_FLAG_HANDOFF | \
	 RWSEM_FLAG_READFAIL)

static inline void rwsem_set_owner(struct rw_semaphore *sem)
{
	atomic_long_set(&sem->owner, (long)current);
}

/* rwsem_test_oflags inlined into is_rwsem_reader_owned */

static inline void __rwsem_set_reader_owned(struct rw_semaphore *sem,
					    struct task_struct *owner)
{
	unsigned long val =
		(unsigned long)owner | RWSEM_READER_OWNED |
		(atomic_long_read(&sem->owner) & RWSEM_NONSPINNABLE);
	atomic_long_set(&sem->owner, val);
}

static inline void rwsem_set_reader_owned(struct rw_semaphore *sem)
{
	__rwsem_set_reader_owned(sem, current);
}
/* is_rwsem_reader_owned removed - never called */
/* rwsem_set_nonspinnable inlined into rwsem_read_trylock */
/* rwsem_read_trylock inlined into __down_read_common */

static inline bool rwsem_write_trylock(struct rw_semaphore *sem)
{
	long tmp = RWSEM_UNLOCKED_VALUE;

	if (atomic_long_try_cmpxchg_acquire(&sem->count, &tmp,
					    RWSEM_WRITER_LOCKED)) {
		rwsem_set_owner(sem);
		return true;
	}

	return false;
}
/* rwsem_owner removed - never called */

void __init_rwsem(struct rw_semaphore *sem, const char *name,
		  struct lock_class_key *key)
{
	atomic_long_set(&sem->count, RWSEM_UNLOCKED_VALUE);
	raw_spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
	atomic_long_set(&sem->owner, 0L);
}

enum rwsem_waiter_type { RWSEM_WAITING_FOR_WRITE, RWSEM_WAITING_FOR_READ };

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
	unsigned long timeout;

	bool handoff_set;
};
#define rwsem_first_waiter(sem) \
	list_first_entry(&sem->wait_list, struct rwsem_waiter, list)

enum rwsem_wake_type {
	RWSEM_WAKE_ANY,
	RWSEM_WAKE_READERS,
	RWSEM_WAKE_READ_OWNED
};

#define RWSEM_WAIT_TIMEOUT DIV_ROUND_UP(HZ, 250)

#define MAX_READERS_WAKEUP 0x100

static inline void rwsem_add_waiter(struct rw_semaphore *sem,
				    struct rwsem_waiter *waiter)
{
	list_add_tail(&waiter->list, &sem->wait_list);
}

/* rwsem_del_waiter inlined into rwsem_del_wake_waiter */

static void rwsem_mark_wake(struct rw_semaphore *sem,
			    enum rwsem_wake_type wake_type,
			    struct wake_q_head *wake_q)
{
	struct rwsem_waiter *waiter, *tmp;
	long oldcount, woken = 0, adjustment = 0;
	struct list_head wlist;

	waiter = rwsem_first_waiter(sem);

	if (waiter->type == RWSEM_WAITING_FOR_WRITE) {
		if (wake_type == RWSEM_WAKE_ANY)
			wake_q_add(wake_q, waiter->task);

		return;
	}

	if (unlikely(atomic_long_read(&sem->count) < 0))
		return;

	if (wake_type != RWSEM_WAKE_READ_OWNED) {
		struct task_struct *owner;

		adjustment = RWSEM_READER_BIAS;
		oldcount = atomic_long_fetch_add(adjustment, &sem->count);
		if (unlikely(oldcount & RWSEM_WRITER_MASK)) {
			if (!(oldcount & RWSEM_FLAG_HANDOFF) &&
			    time_after(jiffies, waiter->timeout))
				adjustment -= RWSEM_FLAG_HANDOFF;

			atomic_long_add(-adjustment, &sem->count);
			return;
		}

		owner = waiter->task;
		__rwsem_set_reader_owned(sem, owner);
	}

	INIT_LIST_HEAD(&wlist);
	list_for_each_entry_safe(waiter, tmp, &sem->wait_list, list) {
		if (waiter->type == RWSEM_WAITING_FOR_WRITE)
			continue;

		woken++;
		list_move_tail(&waiter->list, &wlist);

		if (unlikely(woken >= MAX_READERS_WAKEUP))
			break;
	}

	adjustment = woken * RWSEM_READER_BIAS - adjustment;

	oldcount = atomic_long_read(&sem->count);
	if (list_empty(&sem->wait_list)) {
		adjustment -= RWSEM_FLAG_WAITERS;
		if (oldcount & RWSEM_FLAG_HANDOFF)
			adjustment -= RWSEM_FLAG_HANDOFF;
	} else if (woken) {
		if (oldcount & RWSEM_FLAG_HANDOFF)
			adjustment -= RWSEM_FLAG_HANDOFF;
	}

	if (adjustment)
		atomic_long_add(adjustment, &sem->count);

	list_for_each_entry_safe(waiter, tmp, &wlist, list) {
		struct task_struct *tsk;

		tsk = waiter->task;
		get_task_struct(tsk);

		smp_store_release(&waiter->task, NULL);

		wake_q_add_safe(wake_q, tsk);
	}
}

static inline void rwsem_del_wake_waiter(struct rw_semaphore *sem,
					 struct rwsem_waiter *waiter,
					 struct wake_q_head *wake_q)
	__releases(&sem->wait_lock)
{
	bool first = rwsem_first_waiter(sem) == waiter;
	bool has_waiters;

	wake_q_init(wake_q);

	/* rwsem_del_waiter inlined */
	list_del(&waiter->list);
	has_waiters = !list_empty(&sem->wait_list);
	if (!has_waiters)
		atomic_long_andnot(RWSEM_FLAG_HANDOFF | RWSEM_FLAG_WAITERS,
				   &sem->count);

	if (has_waiters && first)
		rwsem_mark_wake(sem, RWSEM_WAKE_ANY, wake_q);
	raw_spin_unlock_irq(&sem->wait_lock);
	if (!wake_q_empty(wake_q))
		wake_up_q(wake_q);
}

static inline bool rwsem_try_write_lock(struct rw_semaphore *sem,
					struct rwsem_waiter *waiter)
{
	bool first = rwsem_first_waiter(sem) == waiter;
	long count, new;

	count = atomic_long_read(&sem->count);
	do {
		bool has_handoff = !!(count & RWSEM_FLAG_HANDOFF);

		if (has_handoff) {
			if (!first)
				return false;

			waiter->handoff_set = true;
		}

		new = count;

		if (count & RWSEM_LOCK_MASK) {
			if (has_handoff ||
			    (!rt_task(waiter->task) &&
			     !time_after(jiffies, waiter->timeout)))
				return false;

			new |= RWSEM_FLAG_HANDOFF;
		} else {
			new |= RWSEM_WRITER_LOCKED;
			new &= ~RWSEM_FLAG_HANDOFF;

			if (list_is_singular(&sem->wait_list))
				new &= ~RWSEM_FLAG_WAITERS;
		}
	} while (!atomic_long_try_cmpxchg_acquire(&sem->count, &count, new));

	if (new &RWSEM_FLAG_HANDOFF) {
		waiter->handoff_set = true;
		return false;
	}

	list_del(&waiter->list);
	rwsem_set_owner(sem);
	return true;
}

/* enum owner_state removed - was unused */

static inline void rwsem_cond_wake_waiter(struct rw_semaphore *sem, long count,
					  struct wake_q_head *wake_q)
{
	enum rwsem_wake_type wake_type;

	if (count & RWSEM_WRITER_MASK)
		return;

	if (count & RWSEM_READER_MASK) {
		wake_type = RWSEM_WAKE_READERS;
	} else {
		wake_type = RWSEM_WAKE_ANY;
	}
	rwsem_mark_wake(sem, wake_type, wake_q);
}

static struct rw_semaphore __sched *
rwsem_down_read_slowpath(struct rw_semaphore *sem, long count,
			 unsigned int state)
{
	long adjustment = -RWSEM_READER_BIAS;
	long rcnt = (count >> RWSEM_READER_SHIFT);
	struct rwsem_waiter waiter;
	DEFINE_WAKE_Q(wake_q);

	if ((atomic_long_read(&sem->owner) & RWSEM_READER_OWNED) &&
	    (rcnt > 1) && !(count & RWSEM_WRITER_LOCKED))
		goto queue;

	if (!(count & (RWSEM_WRITER_LOCKED | RWSEM_FLAG_HANDOFF))) {
		rwsem_set_reader_owned(sem);
		if ((rcnt == 1) && (count & RWSEM_FLAG_WAITERS)) {
			raw_spin_lock_irq(&sem->wait_lock);
			if (!list_empty(&sem->wait_list))
				rwsem_mark_wake(sem, RWSEM_WAKE_READ_OWNED,
						&wake_q);
			raw_spin_unlock_irq(&sem->wait_lock);
			wake_up_q(&wake_q);
		}
		return sem;
	}

queue:
	waiter.task = current;
	waiter.type = RWSEM_WAITING_FOR_READ;
	waiter.timeout = jiffies + RWSEM_WAIT_TIMEOUT;

	raw_spin_lock_irq(&sem->wait_lock);
	if (list_empty(&sem->wait_list)) {
		if (!(atomic_long_read(&sem->count) & RWSEM_WRITER_MASK)) {
			smp_acquire__after_ctrl_dep();
			raw_spin_unlock_irq(&sem->wait_lock);
			rwsem_set_reader_owned(sem);
			return sem;
		}
		adjustment += RWSEM_FLAG_WAITERS;
	}
	rwsem_add_waiter(sem, &waiter);

	count = atomic_long_add_return(adjustment, &sem->count);

	rwsem_cond_wake_waiter(sem, count, &wake_q);
	raw_spin_unlock_irq(&sem->wait_lock);

	if (!wake_q_empty(&wake_q))
		wake_up_q(&wake_q);

	for (;;) {
		set_current_state(state);
		if (!smp_load_acquire(&waiter.task)) {
			break;
		}
		if (signal_pending_state(state, current)) {
			raw_spin_lock_irq(&sem->wait_lock);
			if (waiter.task)
				goto out_nolock;
			raw_spin_unlock_irq(&sem->wait_lock);

			break;
		}
		schedule();
	}

	__set_current_state(TASK_RUNNING);
	return sem;

out_nolock:
	rwsem_del_wake_waiter(sem, &waiter, &wake_q);
	__set_current_state(TASK_RUNNING);
	return ERR_PTR(-EINTR);
}

static struct rw_semaphore __sched *
rwsem_down_write_slowpath(struct rw_semaphore *sem, int state)
{
	struct rwsem_waiter waiter;
	DEFINE_WAKE_Q(wake_q);

	/* rwsem_can_spin_on_owner and rwsem_optimistic_spin always return false - removed */

	waiter.task = current;
	waiter.type = RWSEM_WAITING_FOR_WRITE;
	waiter.timeout = jiffies + RWSEM_WAIT_TIMEOUT;
	waiter.handoff_set = false;

	raw_spin_lock_irq(&sem->wait_lock);
	rwsem_add_waiter(sem, &waiter);

	if (rwsem_first_waiter(sem) != &waiter) {
		rwsem_cond_wake_waiter(sem, atomic_long_read(&sem->count),
				       &wake_q);
		if (!wake_q_empty(&wake_q)) {
			raw_spin_unlock_irq(&sem->wait_lock);
			wake_up_q(&wake_q);
			raw_spin_lock_irq(&sem->wait_lock);
		}
	} else {
		atomic_long_or(RWSEM_FLAG_WAITERS, &sem->count);
	}

	set_current_state(state);

	for (;;) {
		if (rwsem_try_write_lock(sem, &waiter)) {
			break;
		}

		raw_spin_unlock_irq(&sem->wait_lock);

		if (signal_pending_state(state, current))
			goto out_nolock;

		/* waiter.handoff_set spin code removed - rwsem_spin_on_owner was stub */

		schedule();
		set_current_state(state);
		raw_spin_lock_irq(&sem->wait_lock);
	}
	__set_current_state(TASK_RUNNING);
	raw_spin_unlock_irq(&sem->wait_lock);
	return sem;

out_nolock:
	__set_current_state(TASK_RUNNING);
	raw_spin_lock_irq(&sem->wait_lock);
	rwsem_del_wake_waiter(sem, &waiter, &wake_q);
	return ERR_PTR(-EINTR);
}

static struct rw_semaphore *rwsem_wake(struct rw_semaphore *sem)
{
	unsigned long flags;
	DEFINE_WAKE_Q(wake_q);

	raw_spin_lock_irqsave(&sem->wait_lock, flags);

	if (!list_empty(&sem->wait_list))
		rwsem_mark_wake(sem, RWSEM_WAKE_ANY, &wake_q);

	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
	wake_up_q(&wake_q);

	return sem;
}

/* rwsem_downgrade_wake inlined - single caller */

static inline int __down_read_common(struct rw_semaphore *sem, int state)
{
	long count;
	bool trylock_ok;

	/* Inlined rwsem_read_trylock */
	count = atomic_long_add_return_acquire(RWSEM_READER_BIAS, &sem->count);
	if (WARN_ON_ONCE(count < 0)) {
		unsigned long owner = atomic_long_read(&sem->owner);
		do {
			if (!(owner & RWSEM_READER_OWNED) ||
			    (owner & RWSEM_NONSPINNABLE))
				break;
		} while (!atomic_long_try_cmpxchg(&sem->owner, &owner,
						  owner | RWSEM_NONSPINNABLE));
	}
	if (!(count & RWSEM_READ_FAILED_MASK)) {
		rwsem_set_reader_owned(sem);
		trylock_ok = true;
	} else {
		trylock_ok = false;
	}

	if (!trylock_ok) {
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

	tmp = atomic_long_read(&sem->count);
	while (!(tmp & RWSEM_READ_FAILED_MASK)) {
		if (atomic_long_try_cmpxchg_acquire(&sem->count, &tmp,
						    tmp + RWSEM_READER_BIAS)) {
			rwsem_set_reader_owned(sem);
			return 1;
		}
	}
	return 0;
}

static inline int __down_write_common(struct rw_semaphore *sem, int state)
{
	if (unlikely(!rwsem_write_trylock(sem))) {
		if (IS_ERR(rwsem_down_write_slowpath(sem, state)))
			return -EINTR;
	}

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
	return rwsem_write_trylock(sem);
}

/* __up_read inlined into up_read */
/* __up_write inlined into up_write */

/* __downgrade_write inlined into downgrade_write */

void __sched down_read(struct rw_semaphore *sem)
{
	LOCK_CONTENDED(sem, __down_read_trylock, __down_read);
}

int __sched down_read_killable(struct rw_semaphore *sem)
{
	if (LOCK_CONTENDED_RETURN(sem, __down_read_trylock,
				  __down_read_killable)) {
		return -EINTR;
	}

	return 0;
}

int down_read_trylock(struct rw_semaphore *sem)
{
	int ret = __down_read_trylock(sem);
	return ret;
}

void __sched down_write(struct rw_semaphore *sem)
{
	LOCK_CONTENDED(sem, __down_write_trylock, __down_write);
}

int __sched down_write_killable(struct rw_semaphore *sem)
{
	if (LOCK_CONTENDED_RETURN(sem, __down_write_trylock,
				  __down_write_killable)) {
		return -EINTR;
	}

	return 0;
}

void up_read(struct rw_semaphore *sem)
{
	/* __up_read inlined */
	long tmp;
	tmp = atomic_long_add_return_release(-RWSEM_READER_BIAS, &sem->count);
	if (unlikely((tmp & (RWSEM_LOCK_MASK | RWSEM_FLAG_WAITERS)) ==
		     RWSEM_FLAG_WAITERS))
		rwsem_wake(sem);
}

void up_write(struct rw_semaphore *sem)
{
	/* __up_write inlined */
	long tmp;

	atomic_long_set(&sem->owner, 0); /* rwsem_clear_owner inlined */
	tmp = atomic_long_fetch_add_release(-RWSEM_WRITER_LOCKED, &sem->count);
	if (unlikely(tmp & RWSEM_FLAG_WAITERS))
		rwsem_wake(sem);
}

/* downgrade_write - called by __do_munmap */
void downgrade_write(struct rw_semaphore *sem)
{
	/* __downgrade_write inlined */
	long tmp;

	tmp = atomic_long_fetch_add_release(
		-RWSEM_WRITER_LOCKED + RWSEM_READER_BIAS, &sem->count);
	rwsem_set_reader_owned(sem);
	if (tmp & RWSEM_FLAG_WAITERS) {
		/* rwsem_downgrade_wake inlined */
		unsigned long flags;
		DEFINE_WAKE_Q(wake_q);
		raw_spin_lock_irqsave(&sem->wait_lock, flags);
		if (!list_empty(&sem->wait_list))
			rwsem_mark_wake(sem, RWSEM_WAKE_READ_OWNED, &wake_q);
		raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
		wake_up_q(&wake_q);
	}
}
