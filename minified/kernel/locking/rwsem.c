// SPDX-License-Identifier: GPL-2.0
/* kernel/rwsem.c: R/W semaphores, public implementation
 *
 * Written by David Howells (dhowells@redhat.com).
 * Derived from asm-i386/semaphore.h
 *
 * Writer lock-stealing by Alex Shi <alex.shi@intel.com>
 * and Michel Lespinasse <walken@google.com>
 *
 * Optimistic spinning by Tim Chen <tim.c.chen@intel.com>
 * and Davidlohr Bueso <davidlohr@hp.com>. Based on mutexes.
 *
 * Rwsem count bit fields re-definition and rwsem rearchitecture by
 * Waiman Long <longman@redhat.com> and
 * Peter Zijlstra <peterz@infradead.org>.
 */

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

/*
 * On 64-bit architectures, the bit definitions of the count are:
 *
 * Bit  0    - writer locked bit
 * Bit  1    - waiters present bit
 * Bit  2    - lock handoff bit
 * Bits 3-7  - reserved
 * Bits 8-62 - 55-bit reader count
 * Bit  63   - read fail bit
 *
 * On 32-bit architectures, the bit definitions of the count are:
 *
 * Bit  0    - writer locked bit
 * Bit  1    - waiters present bit
 * Bit  2    - lock handoff bit
 * Bits 3-7  - reserved
 * Bits 8-30 - 23-bit reader count
 * Bit  31   - read fail bit
 *
 * It is not likely that the most significant bit (read fail bit) will ever
 * be set. This guard bit is still checked anyway in the down_read() fastpath
 * just in case we need to use up more of the reader bits for other purpose
 * in the future.
 *
 * atomic_long_fetch_add() is used to obtain reader lock, whereas
 * atomic_long_cmpxchg() will be used to obtain writer lock.
 *
 * There are three places where the lock handoff bit may be set or cleared.
 * 1) rwsem_mark_wake() for readers		-- set, clear
 * 2) rwsem_try_write_lock() for writers	-- set, clear
 * 3) rwsem_del_waiter()			-- clear
 *
 * For all the above cases, wait_lock will be held. A writer must also
 * be the first one in the wait_list to be eligible for setting the handoff
 * bit. So concurrent setting/clearing of handoff bit is not possible.
 */
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

/*
 * Guide to the rw_semaphore's count field.
 *
 * When the RWSEM_WRITER_LOCKED bit in count is set, the lock is owned
 * by a writer.
 *
 * The lock is owned by readers when
 * (1) the RWSEM_WRITER_LOCKED isn't set in count,
 * (2) some of the reader bits are set in count, and
 * (3) the owner field has RWSEM_READ_OWNED bit set.
 *
 * Having some reader bits set is not enough to guarantee a readers owned
 * lock as the readers may be in the process of backing out from the count
 * and a writer has just released the lock. So another writer may steal
 * the lock immediately after that.
 */

/*
 * Initialize an rwsem:
 */
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

/*
 * release a read lock
 */
void up_read(struct rw_semaphore *sem)
{
	atomic_long_add_return_release(-RWSEM_READER_BIAS, &sem->count);
}

/*
 * release a write lock
 */
void up_write(struct rw_semaphore *sem)
{
	atomic_long_set(&sem->owner, 0);
	atomic_long_fetch_add_release(-RWSEM_WRITER_LOCKED, &sem->count);
}
