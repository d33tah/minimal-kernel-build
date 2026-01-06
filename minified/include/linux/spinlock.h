#ifndef __LINUX_SPINLOCK_H
#define __LINUX_SPINLOCK_H


#include <linux/typecheck.h>
#include <linux/preempt.h>
#include <linux/linkage.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/thread_info.h>
#include <linux/stringify.h>
#include <linux/bottom_half.h>
#include <linux/lockdep.h>
#include <asm/barrier.h>



#include <linux/spinlock_types.h>

#include <asm/processor.h>
#include <asm/barrier.h>
/* arch_spin_is_locked removed - never used */
#define arch_spin_lock(lock)		do { barrier(); (void)(lock); } while (0)
#define arch_spin_unlock(lock)		do { barrier(); (void)(lock); } while (0)
#define arch_spin_trylock(lock)		({ barrier(); (void)(lock); 1; })
/* end spinlock_up.h */

# define raw_spin_lock_init(lock)				\
	do { *(lock) = __RAW_SPIN_LOCK_UNLOCKED(lock); } while (0)

/* raw_spin_is_locked removed - unused */

#ifndef smp_mb__after_spinlock
#define smp_mb__after_spinlock()	do { } while (0)
#endif


#define raw_spin_trylock(lock)	__cond_lock(lock, _raw_spin_trylock(lock))

#define raw_spin_lock(lock)	_raw_spin_lock(lock)

# define raw_spin_lock_nested(lock, subclass)		\
	_raw_spin_lock(((void)(subclass), (lock)))


#define raw_spin_lock_irqsave(lock, flags)		\
	do {						\
		typecheck(unsigned long, flags);	\
		_raw_spin_lock_irqsave(lock, flags);	\
	} while (0)


#define raw_spin_lock_irq(lock)		_raw_spin_lock_irq(lock)
#define raw_spin_lock_bh(lock)		_raw_spin_lock_bh(lock)
#define raw_spin_unlock(lock)		_raw_spin_unlock(lock)
#define raw_spin_unlock_irq(lock)	_raw_spin_unlock_irq(lock)

#define raw_spin_unlock_irqrestore(lock, flags)		\
	do {							\
		typecheck(unsigned long, flags);		\
		_raw_spin_unlock_irqrestore(lock, flags);	\
	} while (0)
#define raw_spin_unlock_bh(lock)	_raw_spin_unlock_bh(lock)


/* raw_spin_trylock_irqsave removed - unused */

/* Inlined from rwlock.h */
# define rwlock_init(lock)					\
	do { *(lock) = __RW_LOCK_UNLOCKED(lock); } while (0)

/* do_raw_read_lock, do_raw_read_trylock, do_raw_read_unlock,
   do_raw_write_lock, do_raw_write_trylock, do_raw_write_unlock removed - unused */

/* read_trylock, write_trylock removed - unused */

#define write_lock(lock)	_raw_write_lock(lock)
#define read_lock(lock)		_raw_read_lock(lock)

/* write_lock_irqsave removed - unused */


#define write_lock_irq(lock)		_raw_write_lock_irq(lock)
#define read_unlock(lock)		_raw_read_unlock(lock)
#define write_unlock(lock)		_raw_write_unlock(lock)
#define write_unlock_irq(lock)		_raw_write_unlock_irq(lock)


/* Inlined from spinlock_api_up.h */
#define assert_raw_spin_locked(lock)	do { (void)(lock); } while (0)
#define ___LOCK(lock) do { __acquire(lock); (void)(lock); } while (0)
#define __LOCK(lock) do { preempt_disable(); ___LOCK(lock); } while (0)
#define __LOCK_BH(lock) do { __local_bh_disable_ip(_THIS_IP_, SOFTIRQ_LOCK_OFFSET); ___LOCK(lock); } while (0)
#define __LOCK_IRQ(lock) do { local_irq_disable(); __LOCK(lock); } while (0)
#define __LOCK_IRQSAVE(lock, flags) do { local_irq_save(flags); __LOCK(lock); } while (0)
#define ___UNLOCK(lock) do { __release(lock); (void)(lock); } while (0)
#define __UNLOCK(lock) do { preempt_enable(); ___UNLOCK(lock); } while (0)
#define __UNLOCK_BH(lock) do { __local_bh_enable_ip(_THIS_IP_, SOFTIRQ_LOCK_OFFSET); ___UNLOCK(lock); } while (0)
#define __UNLOCK_IRQ(lock) do { local_irq_enable(); __UNLOCK(lock); } while (0)
#define __UNLOCK_IRQRESTORE(lock, flags) do { local_irq_restore(flags); __UNLOCK(lock); } while (0)
#define _raw_spin_lock(lock)			__LOCK(lock)
#define _raw_spin_lock_nested(lock, subclass)	__LOCK(lock)
#define _raw_read_lock(lock)			__LOCK(lock)
#define _raw_write_lock(lock)			__LOCK(lock)
#define _raw_write_lock_nested(lock, subclass)	__LOCK(lock)
#define _raw_spin_lock_bh(lock)			__LOCK_BH(lock)
#define _raw_spin_lock_irq(lock)		__LOCK_IRQ(lock)
#define _raw_read_lock_irq(lock)		__LOCK_IRQ(lock)
#define _raw_write_lock_irq(lock)		__LOCK_IRQ(lock)
#define _raw_spin_lock_irqsave(lock, flags)	__LOCK_IRQSAVE(lock, flags)
/* _raw_write_lock_irqsave removed - unused */
#define _raw_spin_trylock(lock)			({ __LOCK(lock); 1; })
/* _raw_read_trylock, _raw_write_trylock removed - unused */
#define _raw_spin_unlock(lock)			__UNLOCK(lock)
#define _raw_read_unlock(lock)			__UNLOCK(lock)
#define _raw_write_unlock(lock)			__UNLOCK(lock)
#define _raw_spin_unlock_bh(lock)		__UNLOCK_BH(lock)
#define _raw_spin_unlock_irq(lock)		__UNLOCK_IRQ(lock)
#define _raw_read_unlock_irq(lock)		__UNLOCK_IRQ(lock)
#define _raw_write_unlock_irq(lock)		__UNLOCK_IRQ(lock)
#define _raw_spin_unlock_irqrestore(lock, flags) __UNLOCK_IRQRESTORE(lock, flags)
/* _raw_write_unlock_irqrestore removed - unused */
/* End of spinlock_api_up.h */



static __always_inline raw_spinlock_t *spinlock_check(spinlock_t *lock)
{
	return &lock->rlock;
}


# define spin_lock_init(_lock)			\
do {						\
	spinlock_check(_lock);			\
	*(_lock) = __SPIN_LOCK_UNLOCKED(_lock);	\
} while (0)


static __always_inline void spin_lock(spinlock_t *lock)
{
	raw_spin_lock(&lock->rlock);
}

static __always_inline void spin_lock_bh(spinlock_t *lock)
{
	raw_spin_lock_bh(&lock->rlock);
}

static __always_inline int spin_trylock(spinlock_t *lock)
{
	return raw_spin_trylock(&lock->rlock);
}

#define spin_lock_nested(lock, subclass)			\
do {								\
	raw_spin_lock_nested(spinlock_check(lock), subclass);	\
} while (0)

static __always_inline void spin_lock_irq(spinlock_t *lock)
{
	raw_spin_lock_irq(&lock->rlock);
}

#define spin_lock_irqsave(lock, flags)				\
do {								\
	raw_spin_lock_irqsave(spinlock_check(lock), flags);	\
} while (0)

static __always_inline void spin_unlock(spinlock_t *lock)
{
	raw_spin_unlock(&lock->rlock);
}

static __always_inline void spin_unlock_bh(spinlock_t *lock)
{
	raw_spin_unlock_bh(&lock->rlock);
}

static __always_inline void spin_unlock_irq(spinlock_t *lock)
{
	raw_spin_unlock_irq(&lock->rlock);
}

static __always_inline void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	raw_spin_unlock_irqrestore(&lock->rlock, flags);
}

/* spin_trylock_irqsave removed - unused */

#define assert_spin_locked(lock)	assert_raw_spin_locked(&(lock)->rlock)


#include <linux/atomic.h>
extern int _atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock);
#define atomic_dec_and_lock(atomic, lock) \
		__cond_lock(lock, _atomic_dec_and_lock(atomic, lock))

extern int _atomic_dec_and_lock_irqsave(atomic_t *atomic, spinlock_t *lock,
					unsigned long *flags);
#define atomic_dec_and_lock_irqsave(atomic, lock, flags) \
		__cond_lock(lock, _atomic_dec_and_lock_irqsave(atomic, lock, &(flags)))

#endif  
