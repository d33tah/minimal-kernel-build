 
#ifndef IOPRIO_H
#define IOPRIO_H

#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/iocontext.h>

#include <uapi/linux/ioprio.h>

 
#define IOPRIO_DEFAULT	IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, IOPRIO_BE_NORM)

 
static inline bool ioprio_valid(unsigned short ioprio)
{
	unsigned short class = IOPRIO_PRIO_CLASS(ioprio);

	return class > IOPRIO_CLASS_NONE && class <= IOPRIO_CLASS_IDLE;
}

 
static inline int task_nice_ioprio(struct task_struct *task)
{
	return (task_nice(task) + 20) / 5;
}

 
static inline int task_nice_ioclass(struct task_struct *task)
{
	if (task->policy == SCHED_IDLE)
		return IOPRIO_CLASS_IDLE;
	else if (task_is_realtime(task))
		return IOPRIO_CLASS_RT;
	else
		return IOPRIO_CLASS_BE;
}

 
static inline int get_current_ioprio(void)
{
	struct io_context *ioc = current->io_context;

	if (ioc)
		return ioc->ioprio;
	return IOPRIO_DEFAULT;
}

 
extern int ioprio_best(unsigned short aprio, unsigned short bprio);

extern int set_task_ioprio(struct task_struct *task, int ioprio);

static inline int ioprio_check_cap(int ioprio)
{
	return -ENOTBLK;
}

#endif
