#ifndef IOPRIO_H
#define IOPRIO_H

#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/iocontext.h>

/* From uapi/linux/ioprio.h - inlined */
#define IOPRIO_CLASS_SHIFT	13
#define IOPRIO_CLASS_MASK	0x07
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)
#define IOPRIO_PRIO_CLASS(ioprio)	\
	(((ioprio) >> IOPRIO_CLASS_SHIFT) & IOPRIO_CLASS_MASK)
#define IOPRIO_PRIO_DATA(ioprio)	((ioprio) & IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class, data)	\
	((((class) & IOPRIO_CLASS_MASK) << IOPRIO_CLASS_SHIFT) | \
	 ((data) & IOPRIO_PRIO_MASK))

enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};

#define IOPRIO_NR_LEVELS	8
#define IOPRIO_BE_NR		IOPRIO_NR_LEVELS

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};

#define IOPRIO_NORM	4
#define IOPRIO_BE_NORM	IOPRIO_NORM

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
