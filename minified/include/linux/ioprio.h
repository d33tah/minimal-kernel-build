#ifndef IOPRIO_H
#define IOPRIO_H

#include <linux/sched.h>
#include <linux/iocontext.h>

/* Minimal ioprio - only get_current_ioprio needed */
/* IOPRIO_DEFAULT = (IOPRIO_CLASS_BE << 13) | IOPRIO_NORM = (2 << 13) | 4 = 16388 */
#define IOPRIO_DEFAULT	16388

static inline int get_current_ioprio(void)
{
	struct io_context *ioc = current->io_context;
	if (ioc)
		return ioc->ioprio;
	return IOPRIO_DEFAULT;
}

#endif
