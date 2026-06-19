#ifndef IOCONTEXT_H
#define IOCONTEXT_H

#include <linux/radix-tree.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>


struct io_context {
	atomic_long_t refcount;
	atomic_t active_ref;
	unsigned short ioprio;
};

#endif
