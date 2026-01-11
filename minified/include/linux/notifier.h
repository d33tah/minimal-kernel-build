#ifndef _LINUX_NOTIFIER_H
#define _LINUX_NOTIFIER_H
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/srcu.h>


struct notifier_block;

typedef	int (*notifier_fn_t)(struct notifier_block *nb,
			unsigned long action, void *data);

struct notifier_block {
	notifier_fn_t notifier_call;
	struct notifier_block __rcu *next;
	int priority;
};

struct atomic_notifier_head {
	spinlock_t lock;
	struct notifier_block __rcu *head;
};

struct blocking_notifier_head {
	struct rw_semaphore rwsem;
	struct notifier_block __rcu *head;
};

struct raw_notifier_head {
	struct notifier_block __rcu *head;
};

/* BLOCKING_INIT_NOTIFIER_HEAD removed - never used */

#define ATOMIC_NOTIFIER_INIT(name) {				\
		.lock = __SPIN_LOCK_UNLOCKED(name.lock),	\
		.head = NULL }
/* RAW_NOTIFIER_INIT removed - never used */

#define ATOMIC_NOTIFIER_HEAD(name)				\
	struct atomic_notifier_head name =			\
		ATOMIC_NOTIFIER_INIT(name)


#ifdef __KERNEL__

/* atomic_notifier_chain_register, blocking_notifier_chain_register removed - never called */


extern int atomic_notifier_call_chain(struct atomic_notifier_head *nh,
		unsigned long val, void *v);
/* blocking_notifier_call_chain, raw_notifier_call_chain removed - never called */

#define NOTIFY_DONE		0x0000
#define NOTIFY_OK		0x0001
#define NOTIFY_STOP_MASK	0x8000
#define NOTIFY_STOP		(NOTIFY_OK|NOTIFY_STOP_MASK)



#endif
#endif
