#ifndef _LINUX_KMSG_DUMP_H
#define _LINUX_KMSG_DUMP_H

/*
 * CONFIG_PRINTK is unset, so kmsg_dump() is a no-op that discards its
 * argument and there is no log buffer to dump. struct kmsg_dumper was
 * never instantiated or registered (no by-address use anywhere), so it
 * and its supporting list.h/errno.h includes have been dropped. The enum
 * is kept only to type the three live KMSG_DUMP_* arguments at the no-op
 * kmsg_dump() callsites (panic.c, reboot.c); the values are never read.
 */
enum kmsg_dump_reason {
	KMSG_DUMP_PANIC,
	KMSG_DUMP_OOPS,
	KMSG_DUMP_EMERG,
};

static inline void kmsg_dump(enum kmsg_dump_reason reason)
{
}


#endif
