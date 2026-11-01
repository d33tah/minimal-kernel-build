// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl, who also corrected VMIN = VTIME = 0.
 *
 * Modified by Theodore Ts'o, 9/14/92, to dynamically allocate the
 * tty_struct and tty_queue structures.  Previously there was an array
 * of 256 tty_struct's which was statically allocated, and the
 * tty_queue structures were allocated at boot time.  Both are now
 * dynamically allocated only when the tty is open.
 *
 * Also restructured routines so that there is more of a separation
 * between the high-level tty routines (tty_io.c and tty_ioctl.c) and
 * the low-level tty routines (serial.c, pty.c, console.c).  This
 * makes for cleaner and more compact code.  -TYT, 9/17/92
 *
 * Modified by Fred N. van Kempen, 01/29/93, to add line disciplines
 * which can be dynamically activated and de-activated by the line
 * discipline handling modules (like SLIP).
 *
 * NOTE: pay no attention to the line discipline code (yet); its
 * interface is still subject to change in this version...
 * -- TYT, 1/31/92
 *
 * Added functionality to the OPOST tty handling.  No delays, but all
 * other bits should be there.
 *	-- Nick Holloway <alfie@dcs.warwick.ac.uk>, 27th May 1993.
 *
 * Rewrote canonical mode and added more termios flags.
 *	-- julian@uhunix.uhcc.hawaii.edu (J. Cowley), 13Jan94
 *
 * Reorganized FASYNC support so mouse code can share it.
 *	-- ctm@ardi.com, 9Sep95
 *
 * New TIOCLINUX variants added.
 *	-- mj@k332.feld.cvut.cz, 19-Nov-95
 *
 * Restrict vt switching via ioctl()
 *      -- grif@cs.ucr.edu, 5-Dec-95
 *
 * Move console and virtual terminal code to more appropriate files,
 * implement CONFIG_VT and generalize console device interface.
 *	-- Marko Kohtala <Marko.Kohtala@hut.fi>, March 97
 *
 * Rewrote tty_init_dev and tty_release_dev to eliminate races.
 *	-- Bill Hawes <whawes@star.net>, June 97
 *
 * Added devfs support.
 *      -- C. Scott Ananian <cananian@alumni.princeton.edu>, 13-Jan-1998
 *
 * Added support for a Unix98-style ptmx device.
 *      -- C. Scott Ananian <cananian@alumni.princeton.edu>, 14-Jan-1998
 *
 * Reduced memory usage for older ARM systems
 *      -- Russell King <rmk@arm.linux.org.uk>
 *
 * Move do_SAK() into process context.  Less stack use in devfs functions.
 * alloc_tty_struct() always uses kmalloc()
 *			 -- Andrew Morton <andrewm@uow.edu.eu> 17Mar01
 */

#include <linux/tty.h>
#include <linux/console.h>
#include <linux/uio.h>
#include <linux/cdev.h>

#include <linux/vt_kern.h>

static DEFINE_MUTEX(tty_mutex);

static ssize_t tty_write(struct kiocb *, struct iov_iter *);
static int tty_open(struct inode *, struct file *);

static const struct file_operations tty_fops = {
	.write_iter = tty_write,
	.open = tty_open,
};

/*
 * Copy the user buffer in chunks into tty->write_buf and hand each chunk to
 * the driver. A driver that accepts fewer bytes than offered ends the write
 * early: the caller gets the short count, as write(2) allows.
 */
static ssize_t tty_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct tty_struct *tty = iocb->ki_filp->private_data;
	size_t count = iov_iter_count(from);
	ssize_t ret = 0, written = 0;
	unsigned int chunk = 2048;

	mutex_lock(&tty->atomic_write_lock);

	if (count < chunk)
		chunk = count;

	/* write_buf/write_cnt is protected by the atomic_write_lock mutex */
	if (tty->write_cnt < chunk) {
		unsigned char *buf_chunk;

		if (chunk < 1024)
			chunk = 1024;

		buf_chunk = kmalloc(chunk, GFP_KERNEL | __GFP_RETRY_MAYFAIL);
		if (!buf_chunk) {
			ret = -ENOMEM;
			goto out;
		}
		kfree(tty->write_buf);
		tty->write_cnt = chunk;
		tty->write_buf = buf_chunk;
	}

	while (count) {
		size_t size = count;

		if (size > chunk)
			size = chunk;

		ret = -EFAULT;
		if (copy_from_iter(tty->write_buf, size, from) != size)
			break;

		ret = tty->ops->write(tty, tty->write_buf, size);
		if (ret <= 0)
			break;

		written += ret;
		if (ret != size)
			break;

		count -= ret;
	}
	if (written)
		ret = written;
out:
	mutex_unlock(&tty->atomic_write_lock);
	return ret;
}

/**
 * tty_standard_install - usual tty->ops->install
 * @driver: the driver for the tty
 * @tty: the tty
 *
 * If the @driver overrides @tty->ops->install, it still can call this function
 * to perform the standard install operations.
 */
int tty_standard_install(struct tty_driver *driver, struct tty_struct *tty)
{
	driver->ttys[tty->index] = tty;
	return 0;
}

static struct tty_struct *tty_init_dev(struct tty_driver *driver, int idx)
{
	struct tty_struct *tty;
	int retval;

	tty = kzalloc(sizeof(*tty), GFP_KERNEL_ACCOUNT);
	if (!tty)
		return ERR_PTR(-ENOMEM);

	mutex_init(&tty->atomic_write_lock);
	tty->ops = driver->ops;
	tty->index = idx;

	retval = driver->ops->install(driver, tty);
	if (retval < 0) {
		kfree(tty);
		return ERR_PTR(retval);
	}
	return tty;
}

/*
 * /dev/console (5:1) is the only tty device node: it resolves to the driver
 * line of the registered console (the VT). The tty is created on first open
 * and reused afterwards.
 */
static int tty_open(struct inode *inode, struct file *filp)
{
	struct tty_driver *driver;
	struct tty_struct *tty;
	int index = -1;

	mutex_lock(&tty_mutex);
	driver = console_device(&index);
	if (!driver) {
		mutex_unlock(&tty_mutex);
		return -ENODEV;
	}
	tty = driver->ttys[index];
	if (!tty)
		tty = tty_init_dev(driver, index);
	mutex_unlock(&tty_mutex);
	if (IS_ERR(tty))
		return PTR_ERR(tty);

	filp->private_data = tty;
	return 0;
}

struct tty_driver *tty_alloc_driver(unsigned int lines)
{
	struct tty_driver *driver;

	driver = kzalloc(sizeof(*driver), GFP_KERNEL);
	if (!driver)
		return ERR_PTR(-ENOMEM);

	driver->ttys = kcalloc(lines, sizeof(*driver->ttys), GFP_KERNEL);
	if (!driver->ttys) {
		kfree(driver);
		return ERR_PTR(-ENOMEM);
	}
	return driver;
}

static struct cdev console_cdev;

static int __init tty_init(void)
{
	cdev_init(&console_cdev, &tty_fops);
	if (cdev_add(&console_cdev, MKDEV(TTYAUX_MAJOR, 1), 1))
		panic("Couldn't register /dev/console driver\n");
	vty_init();
	return 0;
}
fs_initcall(tty_init);
