#ifndef _LINUX_SIGNAL_H
#define _LINUX_SIGNAL_H

#include <linux/signal_types.h>

struct task_struct;

extern struct kmem_cache *sighand_cachep;

#endif
