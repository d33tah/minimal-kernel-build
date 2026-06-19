#ifndef _LINUX_LOCAL_LOCK_H
#define _LINUX_LOCAL_LOCK_H

/* Inlined from local_lock_internal.h */
#include <linux/percpu-defs.h>
#include <linux/lockdep.h>


typedef struct {
} local_lock_t;

#define INIT_LOCAL_LOCK(lockname)	{ }

#define __local_lock(lock)					\
	do {							\
		preempt_disable();				\
	} while (0)

#define __local_lock_irqsave(lock, flags)			\
	do {							\
		local_irq_save(flags);				\
	} while (0)

#define __local_unlock(lock)					\
	do {							\
		preempt_enable();				\
	} while (0)

#define __local_unlock_irqrestore(lock, flags)			\
	do {							\
		local_irq_restore(flags);			\
} while (0)

#define local_lock_init(lock)		do { } while (0)

#define local_lock(lock)		__local_lock(lock)

#define local_lock_irqsave(lock, flags)				\
	__local_lock_irqsave(lock, flags)

#define local_unlock(lock)		__local_unlock(lock)

#define local_unlock_irqrestore(lock, flags)			\
	__local_unlock_irqrestore(lock, flags)

#endif
