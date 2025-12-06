#ifndef IOCONTEXT_H
#define IOCONTEXT_H

#include <linux/radix-tree.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>

/* Unused enum ICQ_* and struct io_cq removed */

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
