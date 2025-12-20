#ifndef _LINUX_MEMORY_H_
#define _LINUX_MEMORY_H_

#include <linux/node.h>
#include <linux/compiler.h>
#include <linux/mutex.h>


static inline void memory_dev_init(void) { }

extern struct mutex text_mutex;

#endif
