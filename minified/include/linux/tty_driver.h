#ifndef _LINUX_TTY_DRIVER_H
#define _LINUX_TTY_DRIVER_H

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/cdev.h>

struct tty_struct;
struct tty_driver;

struct tty_operations {
	int (*install)(struct tty_driver *driver, struct tty_struct *tty);
	int (*write)(struct tty_struct *tty, const unsigned char *buf,
		     int count);
} __randomize_layout;

struct tty_driver {
	struct cdev **cdevs;
	const char *name;
	int name_base;
	int major;
	int minor_start;
	unsigned int num;

	struct tty_struct **ttys;

	const struct tty_operations *ops;
	struct list_head tty_drivers;
} __randomize_layout;

extern struct list_head tty_drivers;

struct tty_driver *tty_alloc_driver(unsigned int lines);

static inline void tty_set_operations(struct tty_driver *driver,
				      const struct tty_operations *op)
{
	driver->ops = op;
}

int tty_register_driver(struct tty_driver *driver);
struct device *tty_register_device(struct tty_driver *driver, unsigned index,
				   struct device *dev);

#endif
