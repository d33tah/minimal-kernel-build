/* --- 2026-02-12 --- Reduced: port is initialized but never used.
 * Keep struct for vc_data embedding (via tty_port_init). */
#ifndef _LINUX_TTY_PORT_H
#define _LINUX_TTY_PORT_H

#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/tty_buffer.h>

struct tty_port {
	struct tty_bufhead	buf;
	spinlock_t		lock;
	struct mutex		mutex;
	struct mutex		buf_mutex;
	struct kref		kref;
};

void tty_port_init(struct tty_port *port);

#endif
