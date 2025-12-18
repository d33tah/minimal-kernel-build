#ifndef IOPRIO_H
#define IOPRIO_H

#include <linux/sched.h>
#include <linux/iocontext.h>

/* Simplified - most ioprio infrastructure removed as unused */

#define IOPRIO_CLASS_SHIFT	13
#define IOPRIO_CLASS_MASK	0x07
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)
#define IOPRIO_PRIO_VALUE(class, data)	\
	((((class) & IOPRIO_CLASS_MASK) << IOPRIO_CLASS_SHIFT) | \
	 ((data) & IOPRIO_PRIO_MASK))

enum { IOPRIO_CLASS_NONE, IOPRIO_CLASS_RT, IOPRIO_CLASS_BE, IOPRIO_CLASS_IDLE };

#define IOPRIO_NR_LEVELS	8
#define IOPRIO_BE_NR		IOPRIO_NR_LEVELS
#define IOPRIO_NORM	4
#define IOPRIO_BE_NORM	IOPRIO_NORM
#define IOPRIO_DEFAULT	IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, IOPRIO_BE_NORM)

/* Removed unused:
   - IOPRIO_PRIO_CLASS, IOPRIO_PRIO_DATA macros
   - IOPRIO_WHO_* enum
   - ioprio_valid, task_nice_ioprio, task_nice_ioclass functions
   - ioprio_check_cap function */

static inline int get_current_ioprio(void)
{
	struct io_context *ioc = current->io_context;
	if (ioc)
		return ioc->ioprio;
	return IOPRIO_DEFAULT;
}

#endif
