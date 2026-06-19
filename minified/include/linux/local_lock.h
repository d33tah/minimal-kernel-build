#ifndef _LINUX_LOCAL_LOCK_H
#define _LINUX_LOCAL_LOCK_H

/* Inlined from local_lock_internal.h */
#include <linux/percpu-defs.h>
#include <linux/lockdep.h>


typedef struct {
} local_lock_t;

static inline void local_lock_acquire(local_lock_t *l) { }
static inline void local_lock_release(local_lock_t *l) { }

#define INIT_LOCAL_LOCK(lockname)	{ }

#define __local_lock(lock)					\
	do {							\
		preempt_disable();				\
		local_lock_acquire(this_cpu_ptr(lock));		\
	} while (0)

#define __local_lock_irq(lock)					\
	do {							\
		local_irq_disable();				\
		local_lock_acquire(this_cpu_ptr(lock));		\
	} while (0)

#define __local_lock_irqsave(lock, flags)			\
	do {							\
		local_irq_save(flags);				\
		local_lock_acquire(this_cpu_ptr(lock));		\
	} while (0)

#define __local_unlock(lock)					\
	do {							\
		local_lock_release(this_cpu_ptr(lock));		\
		preempt_enable();				\
	} while (0)

#define __local_unlock_irq(lock)				\
	do {							\
		local_lock_release(this_cpu_ptr(lock));		\
		local_irq_enable();				\
	} while (0)

#define __local_unlock_irqrestore(lock, flags)			\
	do {							\
		local_lock_release(this_cpu_ptr(lock));		\
		local_irq_restore(flags);			\
} while (0)

#define local_lock_init(lock)		do { } while (0)

#define local_lock(lock)		__local_lock(lock)

#define local_lock_irq(lock)		__local_lock_irq(lock)

#define local_lock_irqsave(lock, flags)				\
	__local_lock_irqsave(lock, flags)

#define local_unlock(lock)		__local_unlock(lock)

#define local_unlock_irq(lock)		__local_unlock_irq(lock)

#define local_unlock_irqrestore(lock, flags)			\
	__local_unlock_irqrestore(lock, flags)

#endif
