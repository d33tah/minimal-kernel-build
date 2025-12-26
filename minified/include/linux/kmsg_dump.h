#ifndef _LINUX_KMSG_DUMP_H
#define _LINUX_KMSG_DUMP_H

#include <linux/errno.h>
#include <linux/list.h>

enum kmsg_dump_reason {
	KMSG_DUMP_UNDEF,
	KMSG_DUMP_PANIC,
	KMSG_DUMP_OOPS,
	KMSG_DUMP_EMERG,
	KMSG_DUMP_SHUTDOWN,
	KMSG_DUMP_MAX
};

struct kmsg_dumper {
	struct list_head list;
	void (*dump)(struct kmsg_dumper *dumper, enum kmsg_dump_reason reason);
	enum kmsg_dump_reason max_reason;
	bool registered;
};

static inline void kmsg_dump(enum kmsg_dump_reason reason)
{
}

#endif  
