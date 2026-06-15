#ifndef _LINUX_SWAIT_H
#define _LINUX_SWAIT_H

#include <linux/list.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <asm/current.h>


struct task_struct;

struct swait_queue_head {
	raw_spinlock_t		lock;
	struct list_head	task_list;
};


#define __SWAIT_QUEUE_HEAD_INITIALIZER(name) {				\
	.lock		= __RAW_SPIN_LOCK_UNLOCKED(name.lock),		\
	.task_list	= LIST_HEAD_INIT((name).task_list),		\
}

#define DECLARE_SWAIT_QUEUE_HEAD(name)					\
	struct swait_queue_head name = __SWAIT_QUEUE_HEAD_INITIALIZER(name)

extern void __init_swait_queue_head(struct swait_queue_head *q, const char *name,
				    struct lock_class_key *key);

#define init_swait_queue_head(q)				\
	do {							\
		static struct lock_class_key __key;		\
		__init_swait_queue_head((q), #q, &__key);	\
	} while (0)

# define DECLARE_SWAIT_QUEUE_HEAD_ONSTACK(name)			\
	DECLARE_SWAIT_QUEUE_HEAD(name)

#endif
