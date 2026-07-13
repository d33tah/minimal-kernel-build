#ifndef _LINUX_NOTIFIER_H
#define _LINUX_NOTIFIER_H
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>


struct notifier_block;

struct atomic_notifier_head { spinlock_t lock; struct notifier_block __rcu *head; };

#define ATOMIC_NOTIFIER_INIT(name) {						.lock = __SPIN_LOCK_UNLOCKED(name.lock),			.head = NULL }

#define ATOMIC_NOTIFIER_HEAD(name)					struct atomic_notifier_head name =					ATOMIC_NOTIFIER_INIT(name)



extern int atomic_notifier_call_chain(struct atomic_notifier_head *nh, unsigned long val, void *v);


#define NOTIFY_DONE		0x0000
#define NOTIFY_OK		0x0001
#define NOTIFY_STOP_MASK	0x8000
#define NOTIFY_STOP		(NOTIFY_OK|NOTIFY_STOP_MASK)




#endif  
