/* --- 2026-02-06 23:00 --- Trimmed: no TTY devices created,
 * most operations dead. Keep struct definitions for type references. */
#ifndef _LINUX_TTY_DRIVER_H
#define _LINUX_TTY_DRIVER_H

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <asm/termios.h>

struct tty_struct;
struct tty_driver;

struct tty_operations {
	int (*write)(struct tty_struct *tty, const unsigned char *buf, int count);
	void (*flush_chars)(struct tty_struct *tty);
} __randomize_layout;

struct tty_driver {
	struct kref kref;
	struct cdev **cdevs;
	struct module	*owner;
	const char	*driver_name;
	const char	*name;
	int	name_base;
	int	major;
	int	minor_start;
	unsigned int	num;
	short	type;
	short	subtype;
	struct ktermios init_termios;
	unsigned long	flags;
	struct tty_struct **ttys;
	struct tty_port **ports;
	struct ktermios **termios;
	const struct tty_operations *ops;
	struct list_head tty_drivers;
} __randomize_layout;

#endif
