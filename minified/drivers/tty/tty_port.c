
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/module.h>

#include "tty.h"

void tty_port_init(struct tty_port *port)
{
	memset(port, 0, sizeof(*port));
	tty_buffer_init(port);
	init_waitqueue_head(&port->open_wait);
	init_waitqueue_head(&port->delta_msr_wait);
	mutex_init(&port->mutex);
	mutex_init(&port->buf_mutex);
	spin_lock_init(&port->lock);
	port->close_delay = (50 * HZ) / 100;
	port->closing_wait = (3000 * HZ) / 100;
	kref_init(&port->kref);
}

void tty_port_destroy(struct tty_port *port)
{
	tty_buffer_cancel_work(port);
	tty_buffer_free_all(port);
}

static void tty_port_destructor(struct kref *kref)
{
	struct tty_port *port = container_of(kref, struct tty_port, kref);

	 
	if (WARN_ON(port->itty))
		return;
	free_page((unsigned long)port->xmit_buf);
	tty_port_destroy(port);
	if (port->ops && port->ops->destruct)
		port->ops->destruct(port);
	else
		kfree(port);
}

void tty_port_put(struct tty_port *port)
{
	if (port)
		kref_put(&port->kref, tty_port_destructor);
}

int tty_port_install(struct tty_port *port, struct tty_driver *driver,
		struct tty_struct *tty)
{
	tty->port = port;
	return tty_standard_install(driver, tty);
}
