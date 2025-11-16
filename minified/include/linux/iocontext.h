 
#ifndef IOCONTEXT_H
#define IOCONTEXT_H

#include <linux/radix-tree.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>

enum {
	ICQ_EXITED		= 1 << 2,
	ICQ_DESTROYED		= 1 << 3,
};

 
struct io_cq {
	struct request_queue	*q;
	struct io_context	*ioc;

	 
	union {
		struct list_head	q_node;
		struct kmem_cache	*__rcu_icq_cache;
	};
	union {
		struct hlist_node	ioc_node;
		struct rcu_head		__rcu_head;
	};

	unsigned int		flags;
};

 
struct io_context {
	atomic_long_t refcount;
	atomic_t active_ref;

	unsigned short ioprio;

};

struct task_struct;
struct io_context;
static inline void put_io_context(struct io_context *ioc) { }
static inline void exit_io_context(struct task_struct *task) { }
static inline int copy_io(unsigned long clone_flags, struct task_struct *tsk)
{
	return 0;
}

#endif  
