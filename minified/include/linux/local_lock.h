#ifndef _LINUX_LOCAL_LOCK_H
#define _LINUX_LOCAL_LOCK_H

/* Inlined from local_lock_internal.h */
#include <linux/percpu-defs.h>
#include <linux/lockdep.h>


typedef struct {
} local_lock_t;

# define LOCAL_LOCK_DEBUG_INIT(lockname)
static inline void local_lock_debug_init(local_lock_t *l) { }

#define INIT_LOCAL_LOCK(lockname)	{ LOCAL_LOCK_DEBUG_INIT(lockname) }

/* __local_lock_init simplified - all components are empty stubs */
#define __local_lock_init(lock)	do { } while (0)

/* local_lock_acquire/release are empty stubs - simplified macros */
#define __local_lock(lock)		preempt_disable()
#define __local_lock_irqsave(lock, flags) local_irq_save(flags)
#define __local_unlock(lock)		preempt_enable()
#define __local_unlock_irqrestore(lock, flags) local_irq_restore(flags)

#define local_lock_init(lock)		__local_lock_init(lock)

#define local_lock(lock)		__local_lock(lock)

#define local_lock_irqsave(lock, flags)				\
	__local_lock_irqsave(lock, flags)

#define local_unlock(lock)		__local_unlock(lock)

#define local_unlock_irqrestore(lock, flags)			\
	__local_unlock_irqrestore(lock, flags)

#endif
