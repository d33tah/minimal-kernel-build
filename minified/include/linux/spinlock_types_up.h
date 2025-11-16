#ifndef __LINUX_SPINLOCK_TYPES_UP_H
#define __LINUX_SPINLOCK_TYPES_UP_H

#ifndef __LINUX_SPINLOCK_TYPES_RAW_H
# error "please don't include this file directly"
#endif

 


typedef struct { } arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED { }


typedef struct {
	 
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED { }

#endif  
