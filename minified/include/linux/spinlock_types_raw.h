#ifndef __LINUX_SPINLOCK_TYPES_RAW_H
#define __LINUX_SPINLOCK_TYPES_RAW_H

typedef struct {
} arch_rwlock_t;
#define __ARCH_RW_LOCK_UNLOCKED \
	{                       \
	}

#include <linux/lockdep_types.h>

typedef struct raw_spinlock {
} raw_spinlock_t;

#define __RAW_SPIN_LOCK_INITIALIZER(lockname) \
	{                                     \
	}

#define __RAW_SPIN_LOCK_UNLOCKED(lockname) \
	(raw_spinlock_t) __RAW_SPIN_LOCK_INITIALIZER(lockname)

#define DEFINE_RAW_SPINLOCK(x) raw_spinlock_t x = __RAW_SPIN_LOCK_UNLOCKED(x)

#endif
