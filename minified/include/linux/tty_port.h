/* --- 2026-02-06 23:00 --- Trimmed: no TTY devices created.
 * Keep struct tty_port (used by vt.c) with minimal fields.
 * Removed kfifo macros, dead function decls, port operations. */
#ifndef _LINUX_TTY_PORT_H
#define _LINUX_TTY_PORT_H

#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/tty_buffer.h>
#include <linux/wait.h>

struct tty_struct;

struct tty_port {
	struct tty_bufhead	buf;
	struct tty_struct	*tty;
	struct tty_struct	*itty;
	spinlock_t		lock;
	int			count;
	unsigned long		flags;
	unsigned long		iflags;
	unsigned char		console:1;
	struct mutex		mutex;
	struct mutex		buf_mutex;
	struct kref		kref;
};

void tty_port_init(struct tty_port *port);

#endif
