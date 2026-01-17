#ifndef _LINUX_KMSG_DUMP_H
#define _LINUX_KMSG_DUMP_H

enum kmsg_dump_reason {
	KMSG_DUMP_PANIC = 1,
	KMSG_DUMP_OOPS,
	KMSG_DUMP_EMERG,
	/* KMSG_DUMP_SHUTDOWN, KMSG_DUMP_MAX removed - unused */
};

/* struct kmsg_dumper removed - never used */

static inline void kmsg_dump(enum kmsg_dump_reason reason)
{
}

#endif  
