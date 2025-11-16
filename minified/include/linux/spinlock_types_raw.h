#ifndef __LINUX_SPINLOCK_TYPES_RAW_H
#define __LINUX_SPINLOCK_TYPES_RAW_H

#include <linux/types.h>

# include <linux/spinlock_types_up.h>

#include <linux/lockdep_types.h>

typedef struct raw_spinlock {
	arch_spinlock_t raw_lock;
} raw_spinlock_t;

#define SPINLOCK_MAGIC		0xdead4ead

#define SPINLOCK_OWNER_INIT	((void *)-1L)

# define RAW_SPIN_DEP_MAP_INIT(lockname)
# define SPIN_DEP_MAP_INIT(lockname)
# define LOCAL_SPIN_DEP_MAP_INIT(lockname)

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
