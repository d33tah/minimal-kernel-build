#ifndef _LINUX_TTY_DRIVER_H
#define _LINUX_TTY_DRIVER_H

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <asm/termios.h>
#include <linux/seq_file.h>

struct tty_struct;
struct tty_driver;
struct serial_icounter_struct;
struct serial_struct;

struct tty_operations {
	struct tty_struct * (*lookup)(struct tty_driver *driver,
			struct file *filp, int idx);
	int  (*install)(struct tty_driver *driver, struct tty_struct *tty);
	void (*remove)(struct tty_driver *driver, struct tty_struct *tty);
	int  (*open)(struct tty_struct * tty, struct file * filp);
	void (*close)(struct tty_struct * tty, struct file * filp);
	void (*shutdown)(struct tty_struct *tty);
	void (*cleanup)(struct tty_struct *tty);
	int  (*write)(struct tty_struct * tty,
		      const unsigned char *buf, int count);
	void (*flush_chars)(struct tty_struct *tty);
	unsigned int (*write_room)(struct tty_struct *tty);
	/* put_char, chars_in_buffer, ioctl, compat_ioctl, set_termios, throttle,
	 * unthrottle, stop, start, hangup, break_ctl, flush_buffer, set_ldisc,
	 * wait_until_sent, send_xchar, resize, tiocmget, tiocmset, get_icount,
	 * get_serial, set_serial, show_fdinfo, proc_show removed - never dispatched */
} __randomize_layout;

struct tty_driver {
	int	magic;
	struct kref kref;
	struct cdev **cdevs;
	struct module	*owner;
	const char	*name;
	int	name_base;
	int	major;
	int	minor_start;
	unsigned int	num;
	struct ktermios init_termios;
	unsigned long	flags;
	struct tty_driver *other;

	 
	struct tty_struct **ttys;
	struct tty_port **ports;
	struct ktermios **termios;



	const struct tty_operations *ops;
	struct list_head tty_drivers;
} __randomize_layout;

extern struct list_head tty_drivers;

struct tty_driver *__tty_alloc_driver(unsigned int lines, struct module *owner,
		unsigned long flags);

void tty_driver_kref_put(struct tty_driver *driver);

#define tty_alloc_driver(lines, flags) \
		__tty_alloc_driver(lines, THIS_MODULE, flags)

static inline struct tty_driver *tty_driver_kref_get(struct tty_driver *d)
{
	kref_get(&d->kref);
	return d;
}

static inline void tty_set_operations(struct tty_driver *driver,
		const struct tty_operations *op)
{
	driver->ops = op;
}

#define TTY_DRIVER_MAGIC		0x5402

#define TTY_DRIVER_INSTALLED		0x0001
#define TTY_DRIVER_RESET_TERMIOS	0x0002
/* REAL_RAW/DYNAMIC_DEV/DEVPTS_MEM/DYNAMIC_ALLOC/UNNUMBERED_NODE removed:
 * the sole driver (console) is allocated with only REAL_RAW|RESET_TERMIOS
 * (REAL_RAW itself was never tested), so every other flag was either
 * write-only or gated an always-false branch. */

int tty_register_driver(struct tty_driver *driver);
struct device *tty_register_device(struct tty_driver *driver, unsigned index,
		struct device *dev);

#endif  
