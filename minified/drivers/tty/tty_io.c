
#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
/* linux/interrupt.h removed - no interrupt features used */
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_buffer.h>
#include <linux/tty_port.h>
#include <linux/file.h>
/* fdtable.h removed - not used */
#include <linux/console.h>
/* linux/ctype.h removed - no ctype functions used */
/* linux/kd.h removed - no kd definitions used */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/init.h>
/* linux/module.h removed - no module features used */
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/bitops.h>
/* delay.h removed - msleep/udelay not used */
#include <linux/ratelimit.h>
#include <linux/compat.h>

#include <linux/uaccess.h>

/* linux/tty.h and linux/interrupt.h already included above */
#include <linux/vt_kern.h>
/* linux/selection.h removed - no selection functions used */

#include "tty.h"

/* tty_debug_hangup removed - was empty macro */

/* Forward declarations for static functions used before definition */
static int tty_lock_interruptible(struct tty_struct *tty);
static void tty_lock(struct tty_struct *tty);
static void tty_unlock(struct tty_struct *tty);
static void tty_driver_kref_put(struct tty_driver *driver);

/* tty_std_termios removed - never used after vty_init removal */

LIST_HEAD(tty_drivers);

DEFINE_MUTEX(tty_mutex);

static ssize_t tty_read(struct kiocb *, struct iov_iter *);
static ssize_t tty_write(struct kiocb *, struct iov_iter *);
/* tty_poll removed - poll/select syscalls return ENOSYS */
static int tty_open(struct inode *, struct file *);
/* tty_compat_ioctl removed - never used */
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

/* tty_alloc_file inlined into tty_open */

/* tty_add_file inlined into tty_open - single caller */

static void tty_free_file(struct file *file)
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
/* Removed: get_tty_driver - inlined into tty_open_by_driver */

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

/* console_fops removed - only used by removed vty_init/tty_init */

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
/* __tty_hangup inlined into do_tty_hangup */

static void do_tty_hangup(struct work_struct *work)
{
	struct tty_struct *tty =
		container_of(work, struct tty_struct, hangup_work);

	/* __tty_hangup inlined */
	if (!tty)
		return;

	tty_lock(tty);
	set_bit(TTY_HUPPED, &tty->flags);
	if (tty->ops->hangup)
		tty->ops->hangup(tty);
	tty_unlock(tty);
}

/* tty_hung_up_p inlined into tty_open - single caller */

/* Stub: init only writes to console, never reads */
static ssize_t tty_read(struct kiocb *iocb, struct iov_iter *to)
{
	return 0;
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

/* file_tty_write inlined into tty_write */
static ssize_t tty_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct tty_struct *tty = file_tty(file);
	struct tty_ldisc *ld;
	ssize_t ret;

	if (!tty || !tty->ops->write || tty_io_error(tty))
		return -EIO;

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

/* redirected_tty_write removed - only used by dead console_fops */

/* pty_line_name inlined into tty_register_device_attr */

static ssize_t tty_line_name(struct tty_driver *driver, int index, char *p)
{
	if (driver->flags & TTY_DRIVER_UNNUMBERED_NODE)
		return sprintf(p, "%s", driver->name);
	else
		return sprintf(p, "%s%d", driver->name,
			       index + driver->name_base);
}

/* tty_driver_lookup_tty inlined into tty_open_by_driver */

static void tty_init_termios(struct tty_struct *tty)
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

static int tty_standard_install(struct tty_driver *driver,
				struct tty_struct *tty)
{
	tty_init_termios(tty);
	tty_driver_kref_get(driver);
	tty->count++;
	driver->ttys[tty->index] = tty;
	return 0;
}

/* tty_driver_install_tty inlined - called driver->ops->install or tty_standard_install */
/* tty_driver_remove_tty inlined into release_tty */
/* tty_reopen inlined into tty_open_by_driver */

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
	retval = driver->ops->install ? driver->ops->install(driver, tty) :
					tty_standard_install(driver, tty);
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

/* tty_save_termios inlined into release_tty */
/* tty_flush_works inlined into tty_release_struct */

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
	/* tty_save_termios inlined */
	if (!(tty->driver->flags & TTY_DRIVER_RESET_TERMIOS)) {
		struct ktermios *tp = tty->driver->termios[idx];

		if (tp == NULL) {
			tp = kmalloc(sizeof(*tp), GFP_KERNEL);
			if (tp)
				tty->driver->termios[idx] = tp;
		}
		if (tp)
			*tp = tty->termios;
	}
	/* tty_driver_remove_tty inlined */
	if (tty->driver->ops->remove)
		tty->driver->ops->remove(tty->driver, tty);
	else
		tty->driver->ttys[tty->index] = NULL;

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

/* tty_release_checks, tty_release_struct removed - inlined */

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

	if (!tty->count) {
		/* tty_release_struct inlined */
		tty_ldisc_release(tty);
		/* flush_work calls removed - stubs returning false */
		mutex_lock(&tty_mutex);
		release_tty(tty, idx);
		mutex_unlock(&tty_mutex);
	}

	return 0;
}

/* tty_open_current_tty, tty_lookup_driver inlined into callers */

static struct tty_struct *tty_open_by_driver(dev_t device, struct file *filp)
{
	struct tty_struct *tty;
	struct tty_driver *driver = NULL;
	int index = -1;
	int retval;

	mutex_lock(&tty_mutex);
	/* tty_lookup_driver inlined */
	switch (device) {
	case MKDEV(TTY_MAJOR, 0): {
		extern struct tty_driver *console_driver;
		driver = tty_driver_kref_get(console_driver);
		index = fg_console;
		break;
	}
	case MKDEV(TTYAUX_MAJOR, 1): {
		struct tty_driver *console_driver = console_device(&index);
		if (console_driver) {
			driver = tty_driver_kref_get(console_driver);
			if (driver && filp) {
				filp->f_flags |= O_NONBLOCK;
				break;
			}
		}
		if (driver)
			tty_driver_kref_put(driver);
		mutex_unlock(&tty_mutex);
		return ERR_PTR(-ENODEV);
	}
	default:
		/* get_tty_driver inlined */
		{
			struct tty_driver *p;
			driver = NULL;
			list_for_each_entry(p, &tty_drivers, tty_drivers) {
				dev_t base = MKDEV(p->major, p->minor_start);
				if (device < base || device >= base + p->num)
					continue;
				index = device - base;
				driver = tty_driver_kref_get(p);
				break;
			}
		}
		if (!driver) {
			mutex_unlock(&tty_mutex);
			return ERR_PTR(-ENODEV);
		}
		break;
	}
	if (IS_ERR(driver)) {
		mutex_unlock(&tty_mutex);
		return ERR_CAST(driver);
	}

	/* tty_driver_lookup_tty inlined */
	if (driver->ops->lookup)
		if (!filp)
			tty = ERR_PTR(-EIO);
		else
			tty = driver->ops->lookup(driver, filp, index);
	else
		tty = driver->ttys[index];
	if (!IS_ERR(tty))
		tty_kref_get(tty);
	/* end inline */
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
		/* tty_reopen inlined */
		{
			struct tty_driver *driver = tty->driver;
			struct tty_ldisc *ld;
			retval = 0;

			if (driver->type == TTY_DRIVER_TYPE_PTY &&
			    driver->subtype == PTY_TYPE_MASTER)
				retval = -EIO;
			else if (!tty->count)
				retval = -EAGAIN;
			else {
				ld = tty_ldisc_ref_wait(tty);
				if (ld)
					tty_ldisc_deref(ld);
				else {
					retval = tty_ldisc_lock(tty, 5 * HZ);
					if (!retval) {
						if (!tty->ldisc)
							retval = tty_ldisc_reinit(
								tty,
								tty->termios
									.c_line);
						tty_ldisc_unlock(tty);
					}
				}
				if (retval == 0)
					tty->count++;
			}
		}
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
	/* Inlined tty_alloc_file */
	filp->private_data =
		kmalloc(sizeof(struct tty_file_private), GFP_KERNEL);
	if (!filp->private_data)
		return -ENOMEM;

	/* Inlined tty_open_current_tty - returns NULL unless MKDEV(TTYAUX_MAJOR, 0) */
	if (device == MKDEV(TTYAUX_MAJOR, 0))
		tty = ERR_PTR(-ENXIO);
	else
		tty = tty_open_by_driver(device, filp);

	if (IS_ERR(tty)) {
		tty_free_file(filp);
		retval = PTR_ERR(tty);
		if (retval != -EAGAIN || signal_pending(current))
			return retval;
		schedule();
		goto retry_open;
	}

	/* --- 2026-02-02 10:05 --- Inlined tty_add_file */
	{
		struct tty_file_private *priv = filp->private_data;
		priv->tty = tty;
		spin_lock(&tty->files_lock);
		list_add(&priv->list, &tty->tty_files);
		spin_unlock(&tty->files_lock);
	}

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

		/* tty_hung_up_p inlined */
		if (filp && filp->f_op == &hung_up_tty_fops)
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
	tty->dev = class_find_device_by_devt(
		tty_class, MKDEV(driver->major, driver->minor_start) + idx);

	return tty;
}

struct class *tty_class;

/* tty_cdev_add removed - only called from tty_register_driver/tty_register_device_attr (~14 LOC) */

/* tty_register_device, tty_device_create_release, tty_register_device_attr
   removed - only called from tty_register_driver which was removed (~72 LOC) */

/* Only used internally, make it static */
/* tty_unregister_device removed - only called from tty_register_driver and destruct_tty_driver (~8 LOC) */

/* __tty_alloc_driver removed - only called from vty_init which was removed (~55 LOC) */

static void destruct_tty_driver(struct kref *kref)
{
	struct tty_driver *driver = container_of(kref, struct tty_driver, kref);

	kfree(driver->cdevs);
	kfree(driver->ports);
	kfree(driver->termios);
	kfree(driver->ttys);
	kfree(driver);
}

static void tty_driver_kref_put(struct tty_driver *driver)
{
	kref_put(&driver->kref, destruct_tty_driver);
}

/* tty_register_driver removed - only called from vty_init which was removed (~55 LOC) */

/* tty_class_init removed - class_create hangs with low memory */

/* tty_init removed - never called (~18 LOC) */

/* Merged from tty_mutex.c */
static void tty_lock(struct tty_struct *tty)
{
	if (WARN(tty->magic != TTY_MAGIC, "L Bad %p\n", tty))
		return;
	tty_kref_get(tty);
	mutex_lock(&tty->legacy_mutex);
}
static int tty_lock_interruptible(struct tty_struct *tty)
{
	int ret;
	if (WARN(tty->magic != TTY_MAGIC, "L Bad %p\n", tty))
		return -EIO;
	tty_kref_get(tty);
	ret = mutex_lock_interruptible(&tty->legacy_mutex);
	if (ret)
		tty_kref_put(tty);
	return ret;
}
static void tty_unlock(struct tty_struct *tty)
{
	if (WARN(tty->magic != TTY_MAGIC, "U Bad %p\n", tty))
		return;
	mutex_unlock(&tty->legacy_mutex);
	tty_kref_put(tty);
}
