

#ifndef _LINUX_SERDEV_H
#define _LINUX_SERDEV_H

#include <linux/types.h>
#include <linux/device.h>

struct tty_port;
struct tty_driver;

static inline struct device *serdev_tty_port_register(struct tty_port *port,
					   struct device *parent,
					   struct tty_driver *drv, int idx)
{
	return ERR_PTR(-ENODEV);
}

static inline int serdev_tty_port_unregister(struct tty_port *port)
{
	return -ENODEV;
}

#endif
