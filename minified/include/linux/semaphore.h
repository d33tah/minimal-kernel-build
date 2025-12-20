#ifndef __LINUX_SEMAPHORE_H
#define __LINUX_SEMAPHORE_H

#include <linux/list.h>
#include <linux/spinlock.h>

struct semaphore {
	raw_spinlock_t		lock;
	unsigned int		count;
	struct list_head	wait_list;
};

#define __SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.lock		= __RAW_SPIN_LOCK_UNLOCKED((name).lock),	\
	.count		= n,						\
	.wait_list	= LIST_HEAD_INIT((name).wait_list),		\
}

#define DEFINE_SEMAPHORE(name)	\
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)

/* sema_init removed - never called */

extern void down(struct semaphore *sem);
extern int __must_check down_trylock(struct semaphore *sem);
extern void up(struct semaphore *sem);

#endif  
