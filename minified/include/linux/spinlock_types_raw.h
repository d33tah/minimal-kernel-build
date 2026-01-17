#ifndef __LINUX_SPINLOCK_TYPES_RAW_H
#define __LINUX_SPINLOCK_TYPES_RAW_H

#include <linux/types.h>

typedef struct { } arch_spinlock_t;
#define __ARCH_SPIN_LOCK_UNLOCKED { }
typedef struct { } arch_rwlock_t;
#define __ARCH_RW_LOCK_UNLOCKED { }
/* end spinlock_types_up.h */

#include <linux/lockdep_types.h>

typedef struct raw_spinlock {
	arch_spinlock_t raw_lock;
} raw_spinlock_t;

/* SPINLOCK_MAGIC, SPINLOCK_OWNER_INIT removed - unused */
# define RAW_SPIN_DEP_MAP_INIT(lockname)
# define SPIN_DEP_MAP_INIT(lockname)
/* LOCAL_SPIN_DEP_MAP_INIT removed - never used */

# define SPIN_DEBUG_INIT(lockname)

#define __RAW_SPIN_LOCK_INITIALIZER(lockname)	\
{						\
	.raw_lock = __ARCH_SPIN_LOCK_UNLOCKED,	\
	SPIN_DEBUG_INIT(lockname)		\
	RAW_SPIN_DEP_MAP_INIT(lockname) }

#define __RAW_SPIN_LOCK_UNLOCKED(lockname)	\
	(raw_spinlock_t) __RAW_SPIN_LOCK_INITIALIZER(lockname)

#define DEFINE_RAW_SPINLOCK(x)  raw_spinlock_t x = __RAW_SPIN_LOCK_UNLOCKED(x)

#endif  
