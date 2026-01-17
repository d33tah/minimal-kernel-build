
#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/console.h>
#include <linux/ctype.h>
#include <linux/kd.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/ratelimit.h>
#include <linux/compat.h>

#include <linux/uaccess.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>

#include "tty.h"

/* tty_debug_hangup removed - was empty macro */

struct ktermios tty_std_termios = {
	.c_iflag = ICRNL | IXON,
	.c_oflag = OPOST | ONLCR,
	.c_cflag = B38400 | CS8 | CREAD | HUPCL,
	.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE |
		   IEXTEN,
	.c_cc = INIT_C_CC,
	.c_ispeed = 38400,
	.c_ospeed = 38400,

};

LIST_HEAD(tty_drivers);

DEFINE_MUTEX(tty_mutex);

static ssize_t tty_read(struct kiocb *, struct iov_iter *);
static ssize_t tty_write(struct kiocb *, struct iov_iter *);
/* tty_poll removed - poll/select syscalls return ENOSYS */
static int tty_open(struct inode *, struct file *);
#define tty_compat_ioctl NULL
/* tty_fasync removed - fcntl returns EINVAL, FASYNC never set */
static void release_tty(struct tty_struct *tty, int idx);

static void free_tty_struct(struct tty_struct *tty)
{
	tty_ldisc_deinit(tty);
	put_device(tty->dev);
	kvfree(tty->write_buf);
	tty->magic = 0xDEADDEAD;
	kfree(tty);
}

static inline struct tty_struct *file_tty(struct file *file)
{
	return ((struct tty_file_private *)file->private_data)->tty;
}

int tty_alloc_file(struct file *file)
{
	struct tty_file_private *priv;

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	file->private_data = priv;

	return 0;
}

void tty_add_file(struct tty_struct *tty, struct file *file)
{
	struct tty_file_private *priv = file->private_data;

	priv->tty = tty;
	priv->file = file;

	spin_lock(&tty->files_lock);
	list_add(&priv->list, &tty->tty_files);
	spin_unlock(&tty->files_lock);
}

void tty_free_file(struct file *file)
{
	struct tty_file_private *priv = file->private_data;

	file->private_data = NULL;
	kfree(priv);
}

const char *tty_name(const struct tty_struct *tty)
{
	if (!tty)
		return "NULL tty";
	return tty->name;
}

const char *tty_driver_name(const struct tty_struct *tty)
{
	if (!tty || !tty->driver)
		return "";
	return tty->driver->name;
}

/* Removed: tty_paranoia_check, check_tty_count - always returned 0 */

static struct tty_driver *get_tty_driver(dev_t device, int *index)
{
	struct tty_driver *p;

	list_for_each_entry(p, &tty_drivers, tty_drivers) {
		dev_t base = MKDEV(p->major, p->minor_start);

		if (device < base || device >= base + p->num)
			continue;
		*index = device - base;
		return tty_driver_kref_get(p);
	}
	return NULL;
}

static ssize_t hung_up_tty_read(struct kiocb *iocb, struct iov_iter *to)
{
	return 0;
}

static ssize_t hung_up_tty_write(struct kiocb *iocb, struct iov_iter *from)
{
	return -EIO;
}

/* hung_up_tty_poll removed - poll/select syscalls return ENOSYS */

/* hung_up_tty_ioctl removed - ioctl syscall returns ENOTTY */

/* hung_up_tty_fasync removed - fcntl returns EINVAL, FASYNC never set */

static const struct file_operations tty_fops = {
	/* llseek removed - lseek syscall returns ENOSYS */
	.read_iter = tty_read,
	.write_iter = tty_write,
	/* splice_read/write removed - splice syscall returns ENOSYS */
	/* poll removed - poll/select syscalls return ENOSYS */
	/* unlocked_ioctl/compat_ioctl removed - ioctl returns ENOTTY */
	.open = tty_open,
	.release = tty_release,
	/* fasync removed - fcntl returns EINVAL, FASYNC never set */
};

static const struct file_operations console_fops = {
	/* llseek removed - lseek syscall returns ENOSYS */
	.read_iter = tty_read,
	.write_iter = redirected_tty_write,
	/* splice_read/write removed - splice syscall returns ENOSYS */
	/* poll removed - poll/select syscalls return ENOSYS */
	/* unlocked_ioctl/compat_ioctl removed - ioctl returns ENOTTY */
	.open = tty_open,
	.release = tty_release,
	/* fasync removed - fcntl returns EINVAL, FASYNC never set */
};

static const struct file_operations hung_up_tty_fops = {
	/* llseek removed - lseek syscall returns ENOSYS */
	.read_iter = hung_up_tty_read,
	.write_iter = hung_up_tty_write,
	/* poll removed - poll/select syscalls return ENOSYS */
	/* unlocked_ioctl/compat_ioctl removed - ioctl returns ENOTTY */
	.release = tty_release,
	/* fasync removed - fcntl returns EINVAL, FASYNC never set */
};

/* tty_wakeup removed - only called from removed tty_port_default_wakeup */

static void __tty_hangup(struct tty_struct *tty, int exit_session)
{
	/* Stub: minimal TTY hangup for simple kernel */
	if (!tty)
		return;

	tty_lock(tty);
	set_bit(TTY_HUPPED, &tty->flags);
	if (tty->ops->hangup)
		tty->ops->hangup(tty);
	tty_unlock(tty);
}

static void do_tty_hangup(struct work_struct *work)
{
	struct tty_struct *tty =
		container_of(work, struct tty_struct, hangup_work);

	__tty_hangup(tty, 0);
}

int tty_hung_up_p(struct file *filp)
{
	return (filp && filp->f_op == &hung_up_tty_fops);
}

static int iterate_tty_read(struct tty_ldisc *ld, struct tty_struct *tty,
			    struct file *file, struct iov_iter *to)
{
	int retval = 0;
	void *cookie = NULL;
	unsigned long offset = 0;
	char kernel_buf[64];
	size_t count = iov_iter_count(to);

	do {
		int size, copied;

		size = count > sizeof(kernel_buf) ? sizeof(kernel_buf) : count;
		size = ld->ops->read(tty, file, kernel_buf, size, &cookie,
				     offset);
		if (!size)
			break;

		if (size < 0) {
			if (retval)
				break;
			retval = size;

			if (retval == -EOVERFLOW)
				offset = 0;
			break;
		}

		copied = copy_to_iter(kernel_buf, size, to);
		offset += copied;
		count -= copied;

		if (unlikely(copied != size)) {
			count = 0;
			retval = -EFAULT;
		}
	} while (cookie);

	memzero_explicit(kernel_buf, sizeof(kernel_buf));
	return offset ? offset : retval;
}

static ssize_t tty_read(struct kiocb *iocb, struct iov_iter *to)
{
	int i;
	struct file *file = iocb->ki_filp;
	struct tty_struct *tty = file_tty(file);
	struct tty_ldisc *ld;

	if (!tty || tty_io_error(tty))
		return -EIO;

	ld = tty_ldisc_ref_wait(tty);
	if (!ld)
		return hung_up_tty_read(iocb, to);
	i = -EIO;
	if (ld->ops->read)
		i = iterate_tty_read(ld, tty, file, to);
	tty_ldisc_deref(ld);

	return i;
}

static inline ssize_t
do_tty_write(ssize_t (*write)(struct tty_struct *, struct file *,
			      const unsigned char *, size_t),
	     struct tty_struct *tty, struct file *file, struct iov_iter *from)
{
	size_t count = iov_iter_count(from);
	ssize_t ret, written = 0;
	unsigned int chunk;
	int ndelay = file->f_flags & O_NDELAY;

	if (!mutex_trylock(&tty->atomic_write_lock)) {
		if (ndelay)
			return -EAGAIN;
		if (mutex_lock_interruptible(&tty->atomic_write_lock))
			return -ERESTARTSYS;
	}

	chunk = 2048;
	if (test_bit(TTY_NO_WRITE_SPLIT, &tty->flags))
		chunk = 65536;
	if (count < chunk)
		chunk = count;

	if (tty->write_cnt < chunk) {
		unsigned char *buf_chunk;

		if (chunk < 1024)
			chunk = 1024;

		buf_chunk = kvmalloc(chunk, GFP_KERNEL | __GFP_RETRY_MAYFAIL);
		if (!buf_chunk) {
			ret = -ENOMEM;
			goto out;
		}
		kvfree(tty->write_buf);
		tty->write_cnt = chunk;
		tty->write_buf = buf_chunk;
	}

	for (;;) {
		size_t size = count;

		if (size > chunk)
			size = chunk;

		ret = -EFAULT;
		if (copy_from_iter(tty->write_buf, size, from) != size)
			break;

		ret = write(tty, file, tty->write_buf, size);
		if (ret <= 0)
			break;

		written += ret;
		if (ret > size)
			break;

		if (ret != size)
			iov_iter_revert(from, size - ret);

		count -= ret;
		if (!count)
			break;
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		cond_resched();
	}
	if (written)
		ret = written;
out:
	mutex_unlock(&tty->atomic_write_lock);
	wake_up_interruptible_poll(&tty->write_wait, EPOLLOUT);
	return ret;
}

static ssize_t file_tty_write(struct file *file, struct kiocb *iocb,
			      struct iov_iter *from)
{
	struct tty_struct *tty = file_tty(file);
	struct tty_ldisc *ld;
	ssize_t ret;

	if (!tty || !tty->ops->write || tty_io_error(tty))
		return -EIO;

	/* write_room check removed - ops->write_room callback removed */
	ld = tty_ldisc_ref_wait(tty);
	if (!ld)
		return hung_up_tty_write(iocb, from);
	if (!ld->ops->write)
		ret = -EIO;
	else
		ret = do_tty_write(ld->ops->write, tty, file, from);
	tty_ldisc_deref(ld);
	return ret;
}

static ssize_t tty_write(struct kiocb *iocb, struct iov_iter *from)
{
	return file_tty_write(iocb->ki_filp, iocb, from);
}

/* redirect is never set, so just call tty_write directly */
ssize_t redirected_tty_write(struct kiocb *iocb, struct iov_iter *iter)
{
	return tty_write(iocb, iter);
}

static void pty_line_name(struct tty_driver *driver, int index, char *p)
{
	static const char ptychar[] = "pqrstuvwxyzabcde";
	int i = index + driver->name_base;

	sprintf(p, "%s%c%x",
		driver->subtype == PTY_TYPE_SLAVE ? "tty" : driver->name,
		ptychar[i >> 4 & 0xf], i & 0xf);
}

static ssize_t tty_line_name(struct tty_driver *driver, int index, char *p)
{
	if (driver->flags & TTY_DRIVER_UNNUMBERED_NODE)
		return sprintf(p, "%s", driver->name);
	else
		return sprintf(p, "%s%d", driver->name,
			       index + driver->name_base);
}

static struct tty_struct *tty_driver_lookup_tty(struct tty_driver *driver,
						struct file *file, int idx)
{
	struct tty_struct *tty;

	if (driver->ops->lookup)
		if (!file)
			tty = ERR_PTR(-EIO);
		else
			tty = driver->ops->lookup(driver, file, idx);
	else
		tty = driver->ttys[idx];

	if (!IS_ERR(tty))
		tty_kref_get(tty);
	return tty;
}

void tty_init_termios(struct tty_struct *tty)
{
	struct ktermios *tp;
	int idx = tty->index;

	if (tty->driver->flags & TTY_DRIVER_RESET_TERMIOS)
		tty->termios = tty->driver->init_termios;
	else {
		tp = tty->driver->termios[idx];
		if (tp != NULL) {
			tty->termios = *tp;
			tty->termios.c_line = tty->driver->init_termios.c_line;
		} else
			tty->termios = tty->driver->init_termios;
	}

	tty->termios.c_ispeed = tty_termios_input_baud_rate(&tty->termios);
	tty->termios.c_ospeed = tty_termios_baud_rate(&tty->termios);
}

int tty_standard_install(struct tty_driver *driver, struct tty_struct *tty)
{
	tty_init_termios(tty);
	tty_driver_kref_get(driver);
	tty->count++;
	driver->ttys[tty->index] = tty;
	return 0;
}

static int tty_driver_install_tty(struct tty_driver *driver,
				  struct tty_struct *tty)
{
	return driver->ops->install ? driver->ops->install(driver, tty) :
				      tty_standard_install(driver, tty);
}

static void tty_driver_remove_tty(struct tty_driver *driver,
				  struct tty_struct *tty)
{
	if (driver->ops->remove)
		driver->ops->remove(driver, tty);
	else
		driver->ttys[tty->index] = NULL;
}

static int tty_reopen(struct tty_struct *tty)
{
	struct tty_driver *driver = tty->driver;
	struct tty_ldisc *ld;
	int retval = 0;

	if (driver->type == TTY_DRIVER_TYPE_PTY &&
	    driver->subtype == PTY_TYPE_MASTER)
		return -EIO;

	if (!tty->count)
		return -EAGAIN;

	ld = tty_ldisc_ref_wait(tty);
	if (ld) {
		tty_ldisc_deref(ld);
	} else {
		retval = tty_ldisc_lock(tty, 5 * HZ);
		if (retval)
			return retval;

		if (!tty->ldisc)
			retval = tty_ldisc_reinit(tty, tty->termios.c_line);
		tty_ldisc_unlock(tty);
	}

	if (retval == 0)
		tty->count++;

	return retval;
}

struct tty_struct *tty_init_dev(struct tty_driver *driver, int idx)
{
	struct tty_struct *tty;
	int retval;

	/* try_module_get always returns true - dead check removed */

	tty = alloc_tty_struct(driver, idx);
	if (!tty) {
		retval = -ENOMEM;
		goto err_module_put;
	}

	tty_lock(tty);
	retval = tty_driver_install_tty(driver, tty);
	if (retval < 0)
		goto err_free_tty;

	if (!tty->port)
		tty->port = driver->ports[idx];

	if (WARN_RATELIMIT(
		    !tty->port,
		    "%s: %s driver does not set tty->port. This would crash the kernel. Fix the driver!\n",
		    __func__, tty->driver->name)) {
		retval = -EINVAL;
		goto err_release_lock;
	}

	retval = tty_ldisc_lock(tty, 5 * HZ);
	if (retval)
		goto err_release_lock;
	tty->port->itty = tty;

	retval = tty_ldisc_setup(tty, tty->link);
	if (retval)
		goto err_release_tty;
	tty_ldisc_unlock(tty);

	return tty;

err_free_tty:
	tty_unlock(tty);
	free_tty_struct(tty);
err_module_put:
	module_put(driver->owner);
	return ERR_PTR(retval);

err_release_tty:
	tty_ldisc_unlock(tty);
	tty_info_ratelimited(tty, "ldisc open failed (%d), clearing slot %d\n",
			     retval, idx);
err_release_lock:
	tty_unlock(tty);
	release_tty(tty, idx);
	return ERR_PTR(retval);
}

static void tty_save_termios(struct tty_struct *tty)
{
	struct ktermios *tp;
	int idx = tty->index;

	if (tty->driver->flags & TTY_DRIVER_RESET_TERMIOS)
		return;

	tp = tty->driver->termios[idx];
	if (tp == NULL) {
		tp = kmalloc(sizeof(*tp), GFP_KERNEL);
		if (tp == NULL)
			return;
		tty->driver->termios[idx] = tp;
	}
	*tp = tty->termios;
}

static void tty_flush_works(struct tty_struct *tty)
{
	/* SAK_work flush removed - never scheduled */
	flush_work(&tty->hangup_work);
	if (tty->link)
		flush_work(&tty->link->hangup_work);
}

static void release_one_tty(struct work_struct *work)
{
	struct tty_struct *tty =
		container_of(work, struct tty_struct, hangup_work);
	struct tty_driver *driver = tty->driver;
	struct module *owner = driver->owner;

	if (tty->ops->cleanup)
		tty->ops->cleanup(tty);

	tty->magic = 0;
	tty_driver_kref_put(driver);
	module_put(owner);

	spin_lock(&tty->files_lock);
	list_del_init(&tty->tty_files);
	spin_unlock(&tty->files_lock);

	put_pid(tty->ctrl.pgrp);
	put_pid(tty->ctrl.session);
	free_tty_struct(tty);
}

static void queue_release_one_tty(struct kref *kref)
{
	struct tty_struct *tty = container_of(kref, struct tty_struct, kref);

	INIT_WORK(&tty->hangup_work, release_one_tty);
	schedule_work(&tty->hangup_work);
}

void tty_kref_put(struct tty_struct *tty)
{
	if (tty)
		kref_put(&tty->kref, queue_release_one_tty);
}

static void release_tty(struct tty_struct *tty, int idx)
{
	WARN_ON(tty->index != idx);
	WARN_ON(!mutex_is_locked(&tty_mutex));
	if (tty->ops->shutdown)
		tty->ops->shutdown(tty);
	tty_save_termios(tty);
	tty_driver_remove_tty(tty->driver, tty);
	if (tty->port)
		tty->port->itty = NULL;
	if (tty->link)
		tty->link->port->itty = NULL;
	if (tty->port)
		tty_buffer_cancel_work(tty->port);
	if (tty->link)
		tty_buffer_cancel_work(tty->link->port);

	tty_kref_put(tty->link);
	tty_kref_put(tty);
}

/* tty_release_checks removed - always returned 0 */

static void tty_release_struct(struct tty_struct *tty, int idx)
{
	tty_ldisc_release(tty);

	tty_flush_works(tty);

	mutex_lock(&tty_mutex);
	release_tty(tty, idx);
	mutex_unlock(&tty_mutex);
}

int tty_release(struct inode *inode, struct file *filp)
{
	/* Minimal stub: simplified TTY release */
	struct tty_struct *tty = file_tty(filp);
	int idx;

	tty_lock(tty);
	idx = tty->index;
	/* tty_release_checks always returned 0 - removed dead branch */

	if (tty->ops->close)
		tty->ops->close(tty, filp);

	if (--tty->count < 0)
		tty->count = 0;

	/* Inlined tty_del_file */
	{
		struct tty_file_private *priv = filp->private_data;
		spin_lock(&tty->files_lock);
		list_del(&priv->list);
		spin_unlock(&tty->files_lock);
		tty_free_file(filp);
	}
	tty_unlock(tty);

	if (!tty->count)
		tty_release_struct(tty, idx);

	return 0;
}

static struct tty_struct *tty_open_current_tty(dev_t device, struct file *filp)
{
	if (device != MKDEV(TTYAUX_MAJOR, 0))
		return NULL;
	/* get_current_tty() always returns NULL */
	return ERR_PTR(-ENXIO);
}

static struct tty_driver *tty_lookup_driver(dev_t device, struct file *filp,
					    int *index)
{
	struct tty_driver *driver = NULL;

	switch (device) {
	case MKDEV(TTY_MAJOR, 0): {
		extern struct tty_driver *console_driver;

		driver = tty_driver_kref_get(console_driver);
		*index = fg_console;
		break;
	}
	case MKDEV(TTYAUX_MAJOR, 1): {
		struct tty_driver *console_driver = console_device(index);

		if (console_driver) {
			driver = tty_driver_kref_get(console_driver);
			if (driver && filp) {
				filp->f_flags |= O_NONBLOCK;
				break;
			}
		}
		if (driver)
			tty_driver_kref_put(driver);
		return ERR_PTR(-ENODEV);
	}
	default:
		driver = get_tty_driver(device, index);
		if (!driver)
			return ERR_PTR(-ENODEV);
		break;
	}
	return driver;
}

static struct tty_struct *tty_open_by_driver(dev_t device, struct file *filp)
{
	struct tty_struct *tty;
	struct tty_driver *driver = NULL;
	int index = -1;
	int retval;

	mutex_lock(&tty_mutex);
	driver = tty_lookup_driver(device, filp, &index);
	if (IS_ERR(driver)) {
		mutex_unlock(&tty_mutex);
		return ERR_CAST(driver);
	}

	tty = tty_driver_lookup_tty(driver, filp, index);
	if (IS_ERR(tty)) {
		mutex_unlock(&tty_mutex);
		goto out;
	}

	if (tty) {
		if (tty_port_kopened(tty->port)) {
			tty_kref_put(tty);
			mutex_unlock(&tty_mutex);
			tty = ERR_PTR(-EBUSY);
			goto out;
		}
		mutex_unlock(&tty_mutex);
		retval = tty_lock_interruptible(tty);
		tty_kref_put(tty);
		if (retval) {
			if (retval == -EINTR)
				retval = -ERESTARTSYS;
			tty = ERR_PTR(retval);
			goto out;
		}
		retval = tty_reopen(tty);
		if (retval < 0) {
			tty_unlock(tty);
			tty = ERR_PTR(retval);
		}
	} else {
		tty = tty_init_dev(driver, index);
		mutex_unlock(&tty_mutex);
	}
out:
	tty_driver_kref_put(driver);
	return tty;
}

static int tty_open(struct inode *inode, struct file *filp)
{
	struct tty_struct *tty;
	int retval;
	/* noctty removed - tty_open_proc_set_tty removed */
	dev_t device = inode->i_rdev;
	unsigned saved_flags = filp->f_flags;

	nonseekable_open(inode, filp);

retry_open:
	retval = tty_alloc_file(filp);
	if (retval)
		return -ENOMEM;

	tty = tty_open_current_tty(device, filp);
	if (!tty)
		tty = tty_open_by_driver(device, filp);

	if (IS_ERR(tty)) {
		tty_free_file(filp);
		retval = PTR_ERR(tty);
		if (retval != -EAGAIN || signal_pending(current))
			return retval;
		schedule();
		goto retry_open;
	}

	tty_add_file(tty, filp);

	if (tty->ops->open)
		retval = tty->ops->open(tty, filp);
	else
		retval = -ENODEV;
	filp->f_flags = saved_flags;

	if (retval) {
		tty_unlock(tty);
		tty_release(inode, filp);
		if (retval != -ERESTARTSYS)
			return retval;

		if (signal_pending(current))
			return retval;

		schedule();

		if (tty_hung_up_p(filp))
			filp->f_op = &tty_fops;
		goto retry_open;
	}
	clear_bit(TTY_HUPPED, &tty->flags);

	/* tty_open_proc_set_tty removed - empty stub (and noctty logic no longer needed) */
	tty_unlock(tty);
	return 0;
}

/* tty_poll removed - poll/select syscalls return ENOSYS */

/* tty_fasync removed - fcntl returns EINVAL, FASYNC never set */

/* tiocgwinsz, tty_do_resize, tiocswinsz, tty_pair_get_tty removed
   ioctl syscall returns -ENOTTY directly (~50 LOC) */

/* tty_ioctl removed - ioctl syscall returns ENOTTY */
/* do_SAK_work removed - SAK_work never scheduled in minimal kernel */

static struct device *tty_get_device(struct tty_struct *tty)
{
	dev_t devt = MKDEV(tty->driver->major, tty->driver->minor_start) +
		     tty->index;

	return class_find_device_by_devt(tty_class, devt);
}

struct tty_struct *alloc_tty_struct(struct tty_driver *driver, int idx)
{
	struct tty_struct *tty;

	tty = kzalloc(sizeof(*tty), GFP_KERNEL_ACCOUNT);
	if (!tty)
		return NULL;

	kref_init(&tty->kref);
	tty->magic = TTY_MAGIC;
	if (tty_ldisc_init(tty)) {
		kfree(tty);
		return NULL;
	}
	tty->ctrl.session = NULL;
	tty->ctrl.pgrp = NULL;
	mutex_init(&tty->legacy_mutex);
	init_rwsem(&tty->termios_rwsem);
	init_ldsem(&tty->ldisc_sem);
	init_waitqueue_head(&tty->write_wait);
	init_waitqueue_head(&tty->read_wait);
	INIT_WORK(&tty->hangup_work, do_tty_hangup);
	mutex_init(&tty->atomic_write_lock);
	spin_lock_init(&tty->flow.lock);
	spin_lock_init(&tty->files_lock);
	INIT_LIST_HEAD(&tty->tty_files);
	/* INIT_WORK(&tty->SAK_work) removed - never scheduled */

	tty->driver = driver;
	tty->ops = driver->ops;
	tty->index = idx;
	tty_line_name(driver, idx, tty->name);
	tty->dev = tty_get_device(tty);

	return tty;
}

struct class *tty_class;

static int tty_cdev_add(struct tty_driver *driver, dev_t dev,
			unsigned int index, unsigned int count)
{
	int err;

	driver->cdevs[index] = cdev_alloc();
	if (!driver->cdevs[index])
		return -ENOMEM;
	driver->cdevs[index]->ops = &tty_fops;
	driver->cdevs[index]->owner = driver->owner;
	err = cdev_add(driver->cdevs[index], dev, count);
	if (err)
		kobject_put(&driver->cdevs[index]->kobj);
	return err;
}

struct device *tty_register_device(struct tty_driver *driver, unsigned index,
				   struct device *device)
{
	return tty_register_device_attr(driver, index, device, NULL);
}

static void tty_device_create_release(struct device *dev)
{
	kfree(dev);
}

struct device *tty_register_device_attr(struct tty_driver *driver,
					unsigned index, struct device *device,
					void *drvdata)
{
	char name[64];
	dev_t devt = MKDEV(driver->major, driver->minor_start) + index;
	struct ktermios *tp;
	struct device *dev;
	int retval;

	if (index >= driver->num) {
		pr_err("%s: Attempt to register invalid tty line number (%d)\n",
		       driver->name, index);
		return ERR_PTR(-EINVAL);
	}

	if (driver->type == TTY_DRIVER_TYPE_PTY)
		pty_line_name(driver, index, name);
	else
		tty_line_name(driver, index, name);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->devt = devt;
	dev->class = tty_class;
	dev->parent = device;
	dev->release = tty_device_create_release;
	dev_set_name(dev, "%s", name);
	/* dev->groups removed - field no longer exists */
	dev_set_drvdata(dev, drvdata);
	retval = device_register(dev);
	if (retval)
		goto err_put;

	if (!(driver->flags & TTY_DRIVER_DYNAMIC_ALLOC)) {
		tp = driver->termios[index];
		if (tp) {
			driver->termios[index] = NULL;
			kfree(tp);
		}

		retval = tty_cdev_add(driver, devt, index, 1);
		if (retval)
			goto err_del;
	}

	kobject_uevent(&dev->kobj, KOBJ_ADD);

	return dev;

err_del:
	device_del(dev);
err_put:
	put_device(dev);

	return ERR_PTR(retval);
}

/* Only used internally, make it static */
static void tty_unregister_device(struct tty_driver *driver, unsigned index)
{
	device_destroy(tty_class,
		       MKDEV(driver->major, driver->minor_start) + index);
	if (!(driver->flags & TTY_DRIVER_DYNAMIC_ALLOC)) {
		cdev_del(driver->cdevs[index]);
		driver->cdevs[index] = NULL;
	}
}

struct tty_driver *__tty_alloc_driver(unsigned int lines, struct module *owner,
				      unsigned long flags)
{
	struct tty_driver *driver;
	unsigned int cdevs = 1;
	int err;

	if (!lines || (flags & TTY_DRIVER_UNNUMBERED_NODE && lines > 1))
		return ERR_PTR(-EINVAL);

	driver = kzalloc(sizeof(*driver), GFP_KERNEL);
	if (!driver)
		return ERR_PTR(-ENOMEM);

	kref_init(&driver->kref);
	driver->magic = TTY_DRIVER_MAGIC;
	driver->num = lines;
	driver->owner = owner;
	driver->flags = flags;

	if (!(flags & TTY_DRIVER_DEVPTS_MEM)) {
		driver->ttys =
			kcalloc(lines, sizeof(*driver->ttys), GFP_KERNEL);
		driver->termios =
			kcalloc(lines, sizeof(*driver->termios), GFP_KERNEL);
		if (!driver->ttys || !driver->termios) {
			err = -ENOMEM;
			goto err_free_all;
		}
	}

	if (!(flags & TTY_DRIVER_DYNAMIC_ALLOC)) {
		driver->ports =
			kcalloc(lines, sizeof(*driver->ports), GFP_KERNEL);
		if (!driver->ports) {
			err = -ENOMEM;
			goto err_free_all;
		}
		cdevs = lines;
	}

	driver->cdevs = kcalloc(cdevs, sizeof(*driver->cdevs), GFP_KERNEL);
	if (!driver->cdevs) {
		err = -ENOMEM;
		goto err_free_all;
	}

	return driver;
err_free_all:
	kfree(driver->ports);
	kfree(driver->ttys);
	kfree(driver->termios);
	kfree(driver->cdevs);
	kfree(driver);
	return ERR_PTR(err);
}

static void destruct_tty_driver(struct kref *kref)
{
	struct tty_driver *driver = container_of(kref, struct tty_driver, kref);
	int i;
	struct ktermios *tp;

	if (driver->flags & TTY_DRIVER_INSTALLED) {
		for (i = 0; i < driver->num; i++) {
			tp = driver->termios[i];
			if (tp) {
				driver->termios[i] = NULL;
				kfree(tp);
			}
			if (!(driver->flags & TTY_DRIVER_DYNAMIC_DEV))
				tty_unregister_device(driver, i);
		}
		if (driver->flags & TTY_DRIVER_DYNAMIC_ALLOC)
			cdev_del(driver->cdevs[0]);
	}
	kfree(driver->cdevs);
	kfree(driver->ports);
	kfree(driver->termios);
	kfree(driver->ttys);
	kfree(driver);
}

void tty_driver_kref_put(struct tty_driver *driver)
{
	kref_put(&driver->kref, destruct_tty_driver);
}

int tty_register_driver(struct tty_driver *driver)
{
	int error;
	int i;
	dev_t dev;
	struct device *d;

	if (!driver->major) {
		error = alloc_chrdev_region(&dev, driver->minor_start,
					    driver->num, driver->name);
		if (!error) {
			driver->major = MAJOR(dev);
			driver->minor_start = MINOR(dev);
		}
	} else {
		dev = MKDEV(driver->major, driver->minor_start);
		error = register_chrdev_region(dev, driver->num, driver->name);
	}
	if (error < 0)
		goto err;

	if (driver->flags & TTY_DRIVER_DYNAMIC_ALLOC) {
		error = tty_cdev_add(driver, dev, 0, driver->num);
		if (error)
			goto err_unreg_char;
	}

	mutex_lock(&tty_mutex);
	list_add(&driver->tty_drivers, &tty_drivers);
	mutex_unlock(&tty_mutex);

	if (!(driver->flags & TTY_DRIVER_DYNAMIC_DEV)) {
		for (i = 0; i < driver->num; i++) {
			d = tty_register_device(driver, i, NULL);
			if (IS_ERR(d)) {
				error = PTR_ERR(d);
				goto err_unreg_devs;
			}
		}
	}
	driver->flags |= TTY_DRIVER_INSTALLED;
	return 0;

err_unreg_devs:
	for (i--; i >= 0; i--)
		tty_unregister_device(driver, i);

	mutex_lock(&tty_mutex);
	list_del(&driver->tty_drivers);
	mutex_unlock(&tty_mutex);

err_unreg_char:
	unregister_chrdev_region(dev, driver->num);
err:
	return error;
}

/* tty_class_init removed - class_create hangs with low memory */

static struct cdev tty_cdev, console_cdev;
/* console_sysfs_notify removed - empty stub, call removed from printk.c */
int __init tty_init(void)
{
	cdev_init(&tty_cdev, &tty_fops);
	if (cdev_add(&tty_cdev, MKDEV(TTYAUX_MAJOR, 0), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 0), 1, "/dev/tty") < 0)
		panic("Couldn't register /dev/tty driver\n");
	device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 0), NULL, "tty");

	cdev_init(&console_cdev, &console_fops);
	if (cdev_add(&console_cdev, MKDEV(TTYAUX_MAJOR, 1), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 1), 1, "/dev/console") <
		    0)
		panic("Couldn't register /dev/console driver\n");
	device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 1), NULL, "console");

	vty_init(&console_fops);
	return 0;
}
