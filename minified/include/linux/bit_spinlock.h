 
#ifndef __LINUX_BIT_SPINLOCK_H
#define __LINUX_BIT_SPINLOCK_H

#include <linux/kernel.h>
#include <linux/preempt.h>
#include <linux/atomic.h>
#include <linux/bug.h>

 
static inline void bit_spin_lock(int bitnum, unsigned long *addr)
{
	 
	preempt_disable();
	__acquire(bitlock);
}

 
static inline int bit_spin_trylock(int bitnum, unsigned long *addr)
{
	preempt_disable();
	__acquire(bitlock);
	return 1;
}

 
static inline void bit_spin_unlock(int bitnum, unsigned long *addr)
{
	preempt_enable();
	__release(bitlock);
}

 
static inline void __bit_spin_unlock(int bitnum, unsigned long *addr)
{
	preempt_enable();
	__release(bitlock);
}

 
static inline int bit_spin_is_locked(int bitnum, unsigned long *addr)
{
	return 1;
}

#endif  

