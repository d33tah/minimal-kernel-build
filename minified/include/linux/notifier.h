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

#define BLOCKING_INIT_NOTIFIER_HEAD(name) do {	\
		init_rwsem(&(name)->rwsem);	\
		(name)->head = NULL;		\
	} while (0)


#define ATOMIC_NOTIFIER_INIT(name) {				\
		.lock = __SPIN_LOCK_UNLOCKED(name.lock),	\
		.head = NULL }
#define BLOCKING_NOTIFIER_INIT(name) {				\
		.rwsem = __RWSEM_INITIALIZER((name).rwsem),	\
		.head = NULL }
#define RAW_NOTIFIER_INIT(name)	{				\
		.head = NULL }

#define ATOMIC_NOTIFIER_HEAD(name)				\
	struct atomic_notifier_head name =			\
		ATOMIC_NOTIFIER_INIT(name)
#define BLOCKING_NOTIFIER_HEAD(name)				\
	struct blocking_notifier_head name =			\
		BLOCKING_NOTIFIER_INIT(name)
#define RAW_NOTIFIER_HEAD(name)					\
	struct raw_notifier_head name =				\
		RAW_NOTIFIER_INIT(name)


#ifdef __KERNEL__

extern int atomic_notifier_chain_register(struct atomic_notifier_head *nh,
		struct notifier_block *nb);
extern int blocking_notifier_chain_register(struct blocking_notifier_head *nh,
		struct notifier_block *nb);


extern int atomic_notifier_call_chain(struct atomic_notifier_head *nh,
		unsigned long val, void *v);
extern int blocking_notifier_call_chain(struct blocking_notifier_head *nh,
		unsigned long val, void *v);
extern int raw_notifier_call_chain(struct raw_notifier_head *nh,
		unsigned long val, void *v);


extern bool atomic_notifier_call_chain_is_empty(struct atomic_notifier_head *nh);

#define NOTIFY_DONE		0x0000
#define NOTIFY_OK		0x0001
#define NOTIFY_STOP_MASK	0x8000
#define NOTIFY_STOP		(NOTIFY_OK|NOTIFY_STOP_MASK)




extern struct blocking_notifier_head reboot_notifier_list;

#endif  
#endif  
