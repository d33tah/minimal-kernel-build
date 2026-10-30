#ifndef _LINUX_TTY_H
#define _LINUX_TTY_H

#include <linux/fs.h>
#include <linux/major.h>
#include <linux/tty_driver.h>

/*
 * No line discipline, termios or input buffering: nothing in this system
 * reads from a tty or changes its settings, so a write goes from the file
 * straight to the driver's ->write (tty_io.c). A tty, once installed, is
 * never torn down (the single task never closes its console descriptors).
 */
struct tty_struct {
	struct tty_driver *driver;
	const struct tty_operations *ops;
	int index;

	struct mutex atomic_write_lock;
	void *driver_data;

	unsigned char *write_buf;
	int write_cnt;
};

int __init tty_init(void);

extern struct class *tty_class;

int tty_standard_install(struct tty_driver *driver, struct tty_struct *tty);

#endif
