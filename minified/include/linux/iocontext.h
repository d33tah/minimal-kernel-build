#ifndef IOCONTEXT_H
#define IOCONTEXT_H
#include <linux/types.h>
struct io_context { atomic_long_t refcount; atomic_t active_ref; unsigned short ioprio; };
struct task_struct;
static inline void exit_io_context(struct task_struct *task) { }
static inline int copy_io(unsigned long clone_flags, struct task_struct *tsk) { return 0; }
#endif  
