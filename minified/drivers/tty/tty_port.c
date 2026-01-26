
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_buffer.h>
#include <linux/tty_port.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/module.h>

#include "tty.h"

/* tty_port_default_receive_buf, tty_port_default_wakeup removed - client_ops never read */
/* tty_port_default_client_ops removed - only read from removed receive_buf in tty_buffer.c */

void tty_port_init(struct tty_port *port)
{
	memset(port, 0, sizeof(*port));
	tty_buffer_init(port);
	mutex_init(&port->mutex);
	mutex_init(&port->buf_mutex);
	spin_lock_init(&port->lock);
	/* client_ops not set - never read */
	kref_init(&port->kref);
}

/* tty_port_link_device, tty_port_register_device, tty_port_register_device_attr,
   tty_port_register_device_attr_serdev, tty_port_register_device_serdev,
   tty_port_alloc_xmit_buf, tty_port_free_xmit_buf removed - never called */

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
	/* free_page removed - empty stub */
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

struct tty_struct *tty_port_tty_get(struct tty_port *port)
{
	unsigned long flags;
	struct tty_struct *tty;

	spin_lock_irqsave(&port->lock, flags);
	tty = tty_kref_get(port->tty);
	spin_unlock_irqrestore(&port->lock, flags);
	return tty;
}

void tty_port_tty_set(struct tty_port *port, struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	tty_kref_put(port->tty);
	port->tty = tty_kref_get(tty);
	spin_unlock_irqrestore(&port->lock, flags);
}

/* tty_port_hangup, tty_port_carrier_raised, tty_port_raise_dtr_rts,
   tty_port_lower_dtr_rts, tty_port_tty_wakeup,
   tty_port_block_til_ready, tty_port_close_start/end, tty_port_close, tty_port_open removed - never called */

int tty_port_install(struct tty_port *port, struct tty_driver *driver,
		     struct tty_struct *tty)
{
	tty->port = port;
	return tty_standard_install(driver, tty);
}
