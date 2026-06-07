#ifndef __LINUX_SPINLOCK_TYPES_H
#define __LINUX_SPINLOCK_TYPES_H

#include <linux/types.h>

typedef struct { } arch_spinlock_t;
#define __ARCH_SPIN_LOCK_UNLOCKED { }
typedef struct { } arch_rwlock_t;
#define __ARCH_RW_LOCK_UNLOCKED { }

struct lock_class_key { };
struct lockdep_map { };

typedef struct raw_spinlock {
	arch_spinlock_t raw_lock;
} raw_spinlock_t;

# define RAW_SPIN_DEP_MAP_INIT(lockname)
# define SPIN_DEP_MAP_INIT(lockname)
# define SPIN_DEBUG_INIT(lockname)

#define __RAW_SPIN_LOCK_INITIALIZER(lockname)	\
{						\
	.raw_lock = __ARCH_SPIN_LOCK_UNLOCKED,	\
	SPIN_DEBUG_INIT(lockname)		\
	RAW_SPIN_DEP_MAP_INIT(lockname) }

#define __RAW_SPIN_LOCK_UNLOCKED(lockname)	\
	(raw_spinlock_t) __RAW_SPIN_LOCK_INITIALIZER(lockname)

#define DEFINE_RAW_SPINLOCK(x)  raw_spinlock_t x = __RAW_SPIN_LOCK_UNLOCKED(x)
/* end spinlock_types_raw.h */

typedef struct spinlock {
	union {
		struct raw_spinlock rlock;

	};
} spinlock_t;

#define ___SPIN_LOCK_INITIALIZER(lockname)	\
	{					\
	.raw_lock = __ARCH_SPIN_LOCK_UNLOCKED,	\
	SPIN_DEBUG_INIT(lockname)		\
	SPIN_DEP_MAP_INIT(lockname) }

#define __SPIN_LOCK_INITIALIZER(lockname) \
	{ { .rlock = ___SPIN_LOCK_INITIALIZER(lockname) } }

#define __SPIN_LOCK_UNLOCKED(lockname) \
	(spinlock_t) __SPIN_LOCK_INITIALIZER(lockname)

#define DEFINE_SPINLOCK(x)	spinlock_t x = __SPIN_LOCK_UNLOCKED(x)

# define RW_DEP_MAP_INIT(lockname)
typedef struct {
	arch_rwlock_t raw_lock;
} rwlock_t;
#define __RW_LOCK_UNLOCKED(lockname) \
	(rwlock_t)	{	.raw_lock = __ARCH_RW_LOCK_UNLOCKED,	\
				RW_DEP_MAP_INIT(lockname) }
#define DEFINE_RWLOCK(x)	rwlock_t x = __RW_LOCK_UNLOCKED(x)

#endif
