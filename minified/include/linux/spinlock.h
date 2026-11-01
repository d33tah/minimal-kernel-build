/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SPINLOCK_H
#define __LINUX_SPINLOCK_H

#include <linux/preempt.h>
#include <linux/irqflags.h>

/*
 * Pull the arch_spinlock_t and arch_rwlock_t definitions:
 */
#include <linux/spinlock_types.h>

#define raw_spin_lock_init(lock)                          \
	do {                                              \
		*(lock) = __RAW_SPIN_LOCK_UNLOCKED(lock); \
	} while (0)

/*
 * Define the various spin_lock methods.  Note we define these
 * regardless of whether CONFIG_SMP or CONFIG_PREEMPTION are set. The
 * various methods are defined as nops in the case they are not
 * required.
 */
#define raw_spin_trylock(lock) __cond_lock(lock, _raw_spin_trylock(lock))

#define raw_spin_lock(lock) _raw_spin_lock(lock)

#define raw_spin_lock_irqsave(lock, flags)           \
	do {                                         \
		typecheck(unsigned long, flags);     \
		_raw_spin_lock_irqsave(lock, flags); \
	} while (0)

#define raw_spin_lock_irq(lock) _raw_spin_lock_irq(lock)
#define raw_spin_unlock(lock) _raw_spin_unlock(lock)
#define raw_spin_unlock_irq(lock) _raw_spin_unlock_irq(lock)

#define raw_spin_unlock_irqrestore(lock, flags)           \
	do {                                              \
		typecheck(unsigned long, flags);          \
		_raw_spin_unlock_irqrestore(lock, flags); \
	} while (0)

#define write_lock(lock) _raw_write_lock(lock)
#define read_lock(lock) _raw_read_lock(lock)

#define read_unlock(lock) _raw_read_unlock(lock)
#define write_unlock(lock) _raw_write_unlock(lock)

#define ___LOCK(lock)         \
	do {                  \
		(void)(lock); \
	} while (0)
#define __LOCK(lock)               \
	do {                       \
		preempt_disable(); \
		___LOCK(lock);     \
	} while (0)
#define __LOCK_IRQ(lock)             \
	do {                         \
		local_irq_disable(); \
		__LOCK(lock);        \
	} while (0)
#define __LOCK_IRQSAVE(lock, flags)    \
	do {                           \
		local_irq_save(flags); \
		__LOCK(lock);          \
	} while (0)
#define ___UNLOCK(lock)       \
	do {                  \
		(void)(lock); \
	} while (0)
#define __UNLOCK(lock)            \
	do {                      \
		preempt_enable(); \
		___UNLOCK(lock);  \
	} while (0)
#define __UNLOCK_IRQ(lock)          \
	do {                        \
		local_irq_enable(); \
		__UNLOCK(lock);     \
	} while (0)
#define __UNLOCK_IRQRESTORE(lock, flags)  \
	do {                              \
		local_irq_restore(flags); \
		__UNLOCK(lock);           \
	} while (0)
#define _raw_spin_lock(lock) __LOCK(lock)
#define _raw_read_lock(lock) __LOCK(lock)
#define _raw_write_lock(lock) __LOCK(lock)
#define _raw_spin_lock_irq(lock) __LOCK_IRQ(lock)
#define _raw_spin_lock_irqsave(lock, flags) __LOCK_IRQSAVE(lock, flags)
#define _raw_spin_trylock(lock) \
	({                      \
		__LOCK(lock);   \
		1;              \
	})
#define _raw_spin_unlock(lock) __UNLOCK(lock)
#define _raw_read_unlock(lock) __UNLOCK(lock)
#define _raw_write_unlock(lock) __UNLOCK(lock)
#define _raw_spin_unlock_irq(lock) __UNLOCK_IRQ(lock)
#define _raw_spin_unlock_irqrestore(lock, flags) \
	__UNLOCK_IRQRESTORE(lock, flags)
/* End of spinlock_api_up.h */

static __always_inline raw_spinlock_t *spinlock_check(spinlock_t *lock)
{
	return &lock->rlock;
}

#define spin_lock_init(_lock)                           \
	do {                                            \
		spinlock_check(_lock);                  \
		*(_lock) = __SPIN_LOCK_UNLOCKED(_lock); \
	} while (0)

static __always_inline void spin_lock(spinlock_t *lock)
{
	raw_spin_lock(&lock->rlock);
}

static __always_inline void spin_lock_irq(spinlock_t *lock)
{
	raw_spin_lock_irq(&lock->rlock);
}

#define spin_lock_irqsave(lock, flags)                              \
	do {                                                        \
		raw_spin_lock_irqsave(spinlock_check(lock), flags); \
	} while (0)

static __always_inline void spin_unlock(spinlock_t *lock)
{
	raw_spin_unlock(&lock->rlock);
}

static __always_inline void spin_unlock_irq(spinlock_t *lock)
{
	raw_spin_unlock_irq(&lock->rlock);
}

static __always_inline void spin_unlock_irqrestore(spinlock_t *lock,
						   unsigned long flags)
{
	raw_spin_unlock_irqrestore(&lock->rlock, flags);
}

#endif
