#ifndef _LINUX_TTY_DRIVER_H
#define _LINUX_TTY_DRIVER_H

#include <linux/fs.h>

struct tty_struct;
struct tty_driver;

struct tty_operations {
	int (*install)(struct tty_driver *driver, struct tty_struct *tty);
	int (*write)(struct tty_struct *tty, const unsigned char *buf,
		     int count);
} __randomize_layout;

/* A tty driver: its lines, each with the tty installed on its first open. */
struct tty_driver {
	struct tty_struct **ttys;
	const struct tty_operations *ops;
} __randomize_layout;

struct tty_driver *tty_alloc_driver(unsigned int lines);

static inline void tty_set_operations(struct tty_driver *driver,
				      const struct tty_operations *op)
{
	driver->ops = op;
}

#endif
