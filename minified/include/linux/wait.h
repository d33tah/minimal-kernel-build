#ifndef _LINUX_WAIT_H
#define _LINUX_WAIT_H
#include <linux/list.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>

#include <asm/current.h>

typedef struct wait_queue_entry wait_queue_entry_t;

typedef int (*wait_queue_func_t)(struct wait_queue_entry *wq_entry, unsigned mode, int flags, void *key);
int default_wake_function(struct wait_queue_entry *wq_entry, unsigned mode, int flags, void *key);

#define WQ_FLAG_EXCLUSIVE	0x01
#define WQ_FLAG_BOOKMARK	0x04
#define WQ_FLAG_PRIORITY	0x20

struct wait_queue_entry {
	unsigned int		flags;
	void			*private;
	wait_queue_func_t	func;
	struct list_head	entry;
};

struct wait_queue_head {
	spinlock_t		lock;
	struct list_head	head;
};
typedef struct wait_queue_head wait_queue_head_t;

#define __WAIT_QUEUE_HEAD_INITIALIZER(name) {					\
	.lock		= __SPIN_LOCK_UNLOCKED(name.lock),			\
	.head		= LIST_HEAD_INIT(name.head) }

#define DECLARE_WAIT_QUEUE_HEAD(name) \
	struct wait_queue_head name = __WAIT_QUEUE_HEAD_INITIALIZER(name)

extern void __init_waitqueue_head(struct wait_queue_head *wq_head, const char *name, struct lock_class_key *);

#define init_waitqueue_head(wq_head)						\
	do {									\
		static struct lock_class_key __key;				\
										\
		__init_waitqueue_head((wq_head), #wq_head, &__key);		\
	} while (0)

# define DECLARE_WAIT_QUEUE_HEAD_ONSTACK(name) DECLARE_WAIT_QUEUE_HEAD(name)

static inline int waitqueue_active(struct wait_queue_head *wq_head)
{
	return !list_empty(&wq_head->head);
}

static inline void __add_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry)
{
	struct list_head *head = &wq_head->head;
	struct wait_queue_entry *wq;

	list_for_each_entry(wq, &wq_head->head, entry) {
		if (!(wq->flags & WQ_FLAG_PRIORITY))
			break;
		head = &wq->entry;
	}
	list_add(&wq_entry->entry, head);
}

static inline void __add_wait_queue_entry_tail(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry)
{
	list_add_tail(&wq_entry->entry, &wq_head->head);
}

void __wake_up(struct wait_queue_head *wq_head, unsigned int mode, int nr, void *key);
void __wake_up_locked_key_bookmark(struct wait_queue_head *wq_head,
		unsigned int mode, void *key, wait_queue_entry_t *bookmark);

#define wake_up_all(x)			__wake_up(x, TASK_NORMAL, 0, NULL)

#define ___wait_is_interruptible(state)						\
	(!__builtin_constant_p(state) ||					\
		state == TASK_INTERRUPTIBLE || state == TASK_KILLABLE)		\

extern void init_wait_entry(struct wait_queue_entry *wq_entry, int flags);

#define ___wait_event(wq_head, condition, state, exclusive, ret, cmd)		\
({										\
	__label__ __out;							\
	struct wait_queue_entry __wq_entry;					\
	long __ret = ret;	 				\
										\
	init_wait_entry(&__wq_entry, exclusive ? WQ_FLAG_EXCLUSIVE : 0);	\
	for (;;) {								\
		long __int = prepare_to_wait_event(&wq_head, &__wq_entry, state);\
										\
		if (condition)							\
			break;							\
										\
		if (___wait_is_interruptible(state) && __int) {			\
			__ret = __int;						\
			goto __out;						\
		}								\
										\
		cmd;								\
	}									\
	finish_wait(&wq_head, &__wq_entry);					\
__out:	__ret;									\
})

#define __wait_event(wq_head, condition)					\
	(void)___wait_event(wq_head, condition, TASK_UNINTERRUPTIBLE, 0, 0,	\
			    schedule())

#define wait_event(wq_head, condition)						\
do {										\
	might_sleep();								\
	if (condition)								\
		break;								\
	__wait_event(wq_head, condition);					\
} while (0)

void prepare_to_wait(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry, int state);
long prepare_to_wait_event(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry, int state);
void finish_wait(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry);
int autoremove_wake_function(struct wait_queue_entry *wq_entry, unsigned mode, int sync, void *key);

#endif
