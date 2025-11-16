#ifndef __LINUX_RWLOCK_TYPES_H
#define __LINUX_RWLOCK_TYPES_H

#if !defined(__LINUX_SPINLOCK_TYPES_H)
# error "Do not include directly, include spinlock_types.h"
#endif

# define RW_DEP_MAP_INIT(lockname)

 
typedef struct {
	arch_rwlock_t raw_lock;
} rwlock_t;

#define RWLOCK_MAGIC		0xdeaf1eed

#define __RW_LOCK_UNLOCKED(lockname) \
	(rwlock_t)	{	.raw_lock = __ARCH_RW_LOCK_UNLOCKED,	\
				RW_DEP_MAP_INIT(lockname) }

#define DEFINE_RWLOCK(x)	rwlock_t x = __RW_LOCK_UNLOCKED(x)


#endif  
