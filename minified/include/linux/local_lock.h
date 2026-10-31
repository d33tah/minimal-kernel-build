#ifndef _LINUX_LOCAL_LOCK_H
#define _LINUX_LOCAL_LOCK_H

/* Inlined from local_lock_internal.h */

typedef struct {
} local_lock_t;

#define __local_lock(lock)         \
	do {                       \
		preempt_disable(); \
	} while (0)

#define __local_unlock(lock)      \
	do {                      \
		preempt_enable(); \
	} while (0)

#define local_lock(lock) __local_lock(lock)

#define local_unlock(lock) __local_unlock(lock)

#endif
