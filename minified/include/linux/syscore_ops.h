
#ifndef _LINUX_SYSCORE_OPS_H
#define _LINUX_SYSCORE_OPS_H

#include <linux/list.h>

struct syscore_ops {
	struct list_head node;
	int (*suspend)(void);
	void (*resume)(void);
	void (*shutdown)(void);
};

static inline void syscore_shutdown(void) {}

#endif
