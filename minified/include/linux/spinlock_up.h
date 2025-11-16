#ifndef __LINUX_SPINLOCK_UP_H
#define __LINUX_SPINLOCK_UP_H

#ifndef __LINUX_SPINLOCK_H
# error "please don't include this file directly"
#endif

#include <asm/processor.h>	 
#include <asm/barrier.h>

 

#define arch_spin_is_locked(lock)	((void)(lock), 0)
 
# define arch_spin_lock(lock)		do { barrier(); (void)(lock); } while (0)
# define arch_spin_unlock(lock)	do { barrier(); (void)(lock); } while (0)
# define arch_spin_trylock(lock)	({ barrier(); (void)(lock); 1; })

#define arch_spin_is_contended(lock)	(((void)(lock), 0))

#endif  
