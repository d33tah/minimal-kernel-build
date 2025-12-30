
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/module.h>

struct tty_port;
struct tty_driver;
static inline struct device *serdev_tty_port_register(struct tty_port *port,
						      struct device *parent,
						      struct tty_driver *drv,
						      int idx)
{
	return ERR_PTR(-ENODEV);
}
#include "tty.h"

static int tty_port_default_receive_buf(struct tty_port *port,
					const unsigned char *p,
					const unsigned char *f, size_t count)
{
	int ret;
	struct tty_struct *tty;
	struct tty_ldisc *disc;

	tty = READ_ONCE(port->itty);
	if (!tty)
		return 0;

	disc = tty_ldisc_ref(tty);
	if (!disc)
		return 0;

	ret = tty_ldisc_receive_buf(disc, p, (char *)f, count);

	tty_ldisc_deref(disc);

	return ret;
}

static void tty_port_default_wakeup(struct tty_port *port)
{
	struct tty_struct *tty = tty_port_tty_get(port);

	if (tty) {
		tty_wakeup(tty);
		tty_kref_put(tty);
	}
}

const struct tty_port_client_operations tty_port_default_client_ops = {
	.receive_buf = tty_port_default_receive_buf,
	.write_wakeup = tty_port_default_wakeup,
};

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
	port->client_ops = &tty_port_default_client_ops;
	kref_init(&port->kref);
}

void tty_port_link_device(struct tty_port *port, struct tty_driver *driver,
			  unsigned index)
{
	if (WARN_ON(index >= driver->num))
		return;
	driver->ports[index] = port;
}

struct device *tty_port_register_device(struct tty_port *port,
					struct tty_driver *driver,
					unsigned index, struct device *device)
{
	return tty_port_register_device_attr(port, driver, index, device, NULL,
					     NULL);
}

struct device *
tty_port_register_device_attr(struct tty_port *port, struct tty_driver *driver,
			      unsigned index, struct device *device,
			      void *drvdata,
			      const struct attribute_group **attr_grp)
{
	tty_port_link_device(port, driver, index);
	return tty_register_device_attr(driver, index, device, drvdata,
					attr_grp);
}

struct device *
tty_port_register_device_attr_serdev(struct tty_port *port,
				     struct tty_driver *driver, unsigned index,
				     struct device *device, void *drvdata,
				     const struct attribute_group **attr_grp)
{
	struct device *dev;

	tty_port_link_device(port, driver, index);

	dev = serdev_tty_port_register(port, device, driver, index);
	if (PTR_ERR(dev) != -ENODEV) {
		return dev;
	}

	return tty_register_device_attr(driver, index, device, drvdata,
					attr_grp);
}

/* tty_port_register_device_serdev, tty_port_alloc_xmit_buf, tty_port_free_xmit_buf removed - never called */

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

/* Stub: tty_port_hangup not needed for Hello World console */
void tty_port_hangup(struct tty_port *port)
{
}

/* tty_port_tty_hangup removed - never called (~8 LOC) */

void tty_port_tty_wakeup(struct tty_port *port)
{
	port->client_ops->write_wakeup(port);
}

/* Stub: carrier/dtr_rts not needed for Hello World console */
int tty_port_carrier_raised(struct tty_port *port)
{
	return 1;
}
void tty_port_raise_dtr_rts(struct tty_port *port)
{
}
void tty_port_lower_dtr_rts(struct tty_port *port)
{
}

/* tty_port_block_til_ready, tty_port_close_start/end, tty_port_close, tty_port_open removed - never called */

int tty_port_install(struct tty_port *port, struct tty_driver *driver,
		     struct tty_struct *tty)
{
	tty->port = port;
	return tty_standard_install(driver, tty);
}
