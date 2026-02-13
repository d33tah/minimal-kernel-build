#ifndef _LINUX_LOCAL_LOCK_H
#define _LINUX_LOCAL_LOCK_H

#include <linux/percpu-defs.h>
#include <linux/lockdep.h>

typedef struct {
} local_lock_t;

#define INIT_LOCAL_LOCK(lockname)	{ }

#define __local_lock(lock)		preempt_disable()
#define __local_lock_irqsave(lock, flags) local_irq_save(flags)
#define __local_unlock(lock)		preempt_enable()
#define __local_unlock_irqrestore(lock, flags) local_irq_restore(flags)

#define local_lock(lock)		__local_lock(lock)

#define local_lock_irqsave(lock, flags)				\
	__local_lock_irqsave(lock, flags)

#define local_unlock(lock)		__local_unlock(lock)

#define local_unlock_irqrestore(lock, flags)			\
	__local_unlock_irqrestore(lock, flags)

#endif
