
#include <linux/sched/signal.h>
#include <linux/tty.h>
#include <linux/file.h>
#include <linux/console.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/bitops.h>
#include <linux/ratelimit.h>
#include <linux/compat.h>

#include <linux/vt_kern.h>

#include "tty.h"

#define tty_debug_hangup(tty, f, args...)	do { } while (0)

#define TTY_PARANOIA_CHECK 1
#define CHECK_TTY_COUNT 1

struct ktermios tty_std_termios = {	
	.c_iflag = ICRNL | IXON,
	.c_oflag = OPOST | ONLCR,
	.c_cflag = B38400 | CS8 | CREAD | HUPCL,
	.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK |
		   ECHOCTL | ECHOKE | IEXTEN,
	.c_cc = INIT_C_CC,
	.c_ispeed = 38400,
	.c_ospeed = 38400,
	
};

LIST_HEAD(tty_drivers);			

DEFINE_MUTEX(tty_mutex);

static ssize_t tty_write(struct kiocb *, struct iov_iter *);
static int tty_open(struct inode *, struct file *);
static void release_tty(struct tty_struct *tty, int idx);

static void free_tty_struct(struct tty_struct *tty)
{
	/* Runtime-dead: both callers (tty_init_dev alloc-error path and the
	 * release_one_tty hangup callback) only fire on tty teardown / alloc
	 * failure, neither of which happens on a single-shot boot. Kept static +
	 * referenced so it stays link-live; its tty_ldisc_deinit callee folds. */
}

static inline struct tty_struct *file_tty(struct file *file)
{
	return ((struct tty_file_private *)file->private_data)->tty;
}

void tty_free_file(struct file *file)
{
	struct tty_file_private *priv = file->private_data;

	file->private_data = NULL;
	kfree(priv);
}

const char *tty_name(const struct tty_struct *tty)
{
	return !tty ? "NULL tty" : tty->name;
}

const char *tty_driver_name(const struct tty_struct *tty)
{
	return (!tty || !tty->driver) ? "" : tty->driver->name;
}

static int tty_paranoia_check(struct tty_struct *tty, struct inode *inode,
			      const char *routine)
{
#ifdef TTY_PARANOIA_CHECK
	if (!tty) {
		return 1;
	}
	if (tty->magic != TTY_MAGIC) {
		return 1;
	}
#endif
	return 0;
}

static int check_tty_count(struct tty_struct *tty, const char *routine)
{
#ifdef CHECK_TTY_COUNT
	struct list_head *p;
	int count = 0, kopen_count = 0;

	spin_lock(&tty->files_lock);
	list_for_each(p, &tty->tty_files) {
		count++;
	}
	spin_unlock(&tty->files_lock);
	if (tty->count != (count + kopen_count)) {
		tty_warn(tty, "%s: tty->count(%d) != (#fd's(%d) + #kopen's(%d))\n",
			 routine, tty->count, count, kopen_count);
		return (count + kopen_count);
	}
#endif
	return 0;
}

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


static const struct file_operations tty_fops = {
	.write_iter	= tty_write,
	.open		= tty_open,
	.release	= tty_release,
};

static const struct file_operations console_fops = {
	.write_iter	= redirected_tty_write,
	.open		= tty_open,
	.release	= tty_release,
};

static DEFINE_SPINLOCK(redirect_lock);
static struct file *redirect;

static void tty_update_time(struct timespec64 *time)
{
	time64_t sec = ktime_get_real_seconds();

	
	if ((sec ^ time->tv_sec) & ~7)
		time->tv_sec = sec;
}

static void tty_write_unlock(struct tty_struct *tty)
{
	mutex_unlock(&tty->atomic_write_lock);
	wake_up_interruptible_poll(&tty->write_wait, EPOLLOUT);
}

static int tty_write_lock(struct tty_struct *tty, int ndelay)
{
	if (!mutex_trylock(&tty->atomic_write_lock)) {
		if (ndelay)
			return -EAGAIN;
		if (mutex_lock_interruptible(&tty->atomic_write_lock))
			return -ERESTARTSYS;
	}
	return 0;
}

static inline ssize_t do_tty_write(
	ssize_t (*write)(struct tty_struct *, struct file *, const unsigned char *, size_t),
	struct tty_struct *tty,
	struct file *file,
	struct iov_iter *from)
{
	size_t count = iov_iter_count(from);
	ssize_t ret, written = 0;
	unsigned int chunk;

	ret = tty_write_lock(tty, file->f_flags & O_NDELAY);
	if (ret < 0)
		return ret;

	
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
			iov_iter_revert(from, size-ret);

		count -= ret;
		if (!count)
			break;
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		cond_resched();
	}
	if (written) {
		tty_update_time(&file_inode(file)->i_mtime);
		ret = written;
	}
out: tty_write_unlock(tty);
	return ret;
}


static ssize_t file_tty_write(struct file *file, struct kiocb *iocb, struct iov_iter *from)
{
	struct tty_struct *tty = file_tty(file);
	struct tty_ldisc *ld;
	ssize_t ret;

	if (tty_paranoia_check(tty, file_inode(file), "tty_write"))
		return -EIO;
	if (!tty || !tty->ops->write ||	tty_io_error(tty))
		return -EIO;
	
	if (tty->ops->write_room == NULL)
		tty_err(tty, "missing write_room method\n");
	ld = tty_ldisc_ref_wait(tty);
	if (!ld)
		return -EIO;
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

ssize_t redirected_tty_write(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *p = NULL;

	spin_lock(&redirect_lock);
	if (redirect)
		p = get_file(redirect);
	spin_unlock(&redirect_lock);

	
	if (p) {
		ssize_t res;

		res = file_tty_write(p, iocb, iter);
		fput(p);
		return res;
	}
	return tty_write(iocb, iter);
}

static ssize_t tty_line_name(struct tty_driver *driver, int index, char *p)
{
	return sprintf(p, "%s%d", driver->name,
		       index + driver->name_base);
}

static struct tty_struct *tty_driver_lookup_tty(struct tty_driver *driver,
		struct file *file, int idx)
{
	struct tty_struct *tty;

	/* ops->lookup is never set (sole tty_operations con_ops omits it) */
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
			tty->termios.c_line  = tty->driver->init_termios.c_line;
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

static int tty_reopen(struct tty_struct *tty)
{
	struct tty_ldisc *ld;
	int retval = 0;

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

	if (WARN_RATELIMIT(!tty->port,
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

err_free_tty: tty_unlock(tty);
	free_tty_struct(tty);
err_module_put: return ERR_PTR(retval);

	
err_release_tty: tty_ldisc_unlock(tty);
	tty_info_ratelimited(tty, "ldisc open failed (%d), clearing slot %d\n",
			     retval, idx);
err_release_lock: tty_unlock(tty);
	release_tty(tty, idx);
	return ERR_PTR(retval);
}

static void release_one_tty(struct work_struct *work)
{
	/* Runtime-dead: the workqueue release callback runs only when a tty's
	 * last kref drops (final close), which never happens on a single-shot
	 * boot. Symbol kept link-live for the INIT_WORK() reference in the
	 * (dead) queue_release_one_tty path. */
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
	/* Runtime-dead teardown root: nothing closes a tty on a single-shot
	 * boot. Link-live via the tty_init_dev error path (also dead). */
}

int tty_release(struct inode *inode, struct file *filp)
{
	/* Runtime-dead: no fd/tty is ever closed on a single-shot boot.
	 * Link-live via tty_fops.release; never executes. */
	return 0;
}

static struct tty_struct *tty_open_current_tty(dev_t device, struct file *filp)
{
	/* No controlling tty in this minimal kernel (get_current_tty == NULL). */
	if (device != MKDEV(TTYAUX_MAJOR, 0))
		return NULL;

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
	}
	return driver;
}


static struct tty_struct *tty_open_by_driver(dev_t device,
					     struct file *filp)
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
out: tty_driver_kref_put(driver);
	return tty;
}

static int tty_open(struct inode *inode, struct file *filp)
{
	struct tty_struct *tty;
	struct tty_file_private *priv;
	int retval;
	dev_t device = inode->i_rdev;
	unsigned saved_flags = filp->f_flags;

	nonseekable_open(inode, filp);

retry_open: priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	filp->private_data = priv;

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

	{
		struct tty_file_private *priv = filp->private_data;

		priv->tty = tty;

		spin_lock(&tty->files_lock);
		list_add(&priv->list, &tty->tty_files);
		spin_unlock(&tty->files_lock);
	}

	check_tty_count(tty, __func__);
	tty_debug_hangup(tty, "opening (count=%d)\n", tty->count);

	if (tty->ops->open)
		retval = tty->ops->open(tty, filp);
	else
		retval = -ENODEV;
	filp->f_flags = saved_flags;

	if (retval) {
		tty_debug_hangup(tty, "open error %d, releasing\n", retval);

		tty_unlock(tty); 
		tty_release(inode, filp);
		if (retval != -ERESTARTSYS)
			return retval;

		if (signal_pending(current))
			return retval;

		schedule();

		goto retry_open;
	}

	tty_unlock(tty);
	return 0;
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
	mutex_init(&tty->legacy_mutex);
	init_rwsem(&tty->termios_rwsem);
	init_ldsem(&tty->ldisc_sem);
	init_waitqueue_head(&tty->write_wait);
	init_waitqueue_head(&tty->read_wait);
	mutex_init(&tty->atomic_write_lock);
	spin_lock_init(&tty->files_lock);
	INIT_LIST_HEAD(&tty->tty_files);

	tty->driver = driver;
	tty->ops = driver->ops;
	tty->index = idx;
	tty_line_name(driver, idx, tty->name);

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
	err = cdev_add(driver->cdevs[index], dev, count);
	if (err)
		kobject_put(&driver->cdevs[index]->kobj);
	return err;
}

static void tty_device_create_release(struct device *dev)
{
	dev_dbg(dev, "releasing...\n");
	kfree(dev);
}

/* tty_register_device_attr folded in: its drvdata + attr_grp params were always
   NULL and never consumed (no sysfs group creation on this build). */
struct device *tty_register_device(struct tty_driver *driver,
				   unsigned index, struct device *device)
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

	tty_line_name(driver, index, name);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->parent = device;
	dev->release = tty_device_create_release;
	dev_set_name(dev, "%s", name);

	retval = device_register(dev);
	if (retval)
		goto err_put;

	tp = driver->termios[index];
	if (tp) {
		driver->termios[index] = NULL;
		kfree(tp);
	}

	retval = tty_cdev_add(driver, devt, index, 1);
	if (retval)
		goto err_put;

	return dev;

err_put: put_device(dev);

	return ERR_PTR(retval);
}

/* Removed: tty_unregister_device + the device teardown chain
   (device_destroy/device_unregister/device_del/bus_remove_device) - no tty
   device is ever unregistered and no driver kref is dropped on this build, so
   the entire char-device teardown path was runtime-dead. */

struct tty_driver *__tty_alloc_driver(unsigned int lines, struct module *owner,
		unsigned long flags)
{
	struct tty_driver *driver;
	int err;

	if (!lines)
		return ERR_PTR(-EINVAL);

	driver = kzalloc(sizeof(*driver), GFP_KERNEL);
	if (!driver)
		return ERR_PTR(-ENOMEM);

	kref_init(&driver->kref);
	driver->num = lines;
	driver->flags = flags;

	driver->ttys = kcalloc(lines, sizeof(*driver->ttys), GFP_KERNEL);
	driver->termios = kcalloc(lines, sizeof(*driver->termios), GFP_KERNEL);
	if (!driver->ttys || !driver->termios) {
		err = -ENOMEM;
		goto err_free_all;
	}

	driver->ports = kcalloc(lines, sizeof(*driver->ports), GFP_KERNEL);
	if (!driver->ports) {
		err = -ENOMEM;
		goto err_free_all;
	}
	driver->cdevs = kcalloc(lines, sizeof(*driver->cdevs), GFP_KERNEL);
	if (!driver->cdevs) {
		err = -ENOMEM;
		goto err_free_all;
	}

	return driver;
err_free_all: kfree(driver->ports);
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
		}
		/* cdev_del removed: char-device teardown is runtime-dead */
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

	mutex_lock(&tty_mutex);
	list_add(&driver->tty_drivers, &tty_drivers);
	mutex_unlock(&tty_mutex);

	for (i = 0; i < driver->num; i++) {
		d = tty_register_device(driver, i, NULL);
		if (IS_ERR(d)) {
			error = PTR_ERR(d);
			goto err_unreg_devs;
		}
	}
	driver->flags |= TTY_DRIVER_INSTALLED;
	return 0;

err_unreg_devs: mutex_lock(&tty_mutex);
	list_del(&driver->tty_drivers);
	mutex_unlock(&tty_mutex);

	unregister_chrdev_region(dev, driver->num);
err: return error;
}

static int __init tty_class_init(void)
{
	tty_class = class_create(THIS_MODULE, "tty");
	return IS_ERR(tty_class) ? PTR_ERR(tty_class) : 0;
}

postcore_initcall(tty_class_init);

static struct cdev tty_cdev, console_cdev;

int __init tty_init(void)
{
	cdev_init(&tty_cdev, &tty_fops);
	if (cdev_add(&tty_cdev, MKDEV(TTYAUX_MAJOR, 0), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 0), 1, "/dev/tty") < 0)
		panic("Couldn't register /dev/tty driver\n");
	device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 0), NULL, "tty");

	cdev_init(&console_cdev, &console_fops);
	if (cdev_add(&console_cdev, MKDEV(TTYAUX_MAJOR, 1), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 1), 1, "/dev/console") < 0)
		panic("Couldn't register /dev/console driver\n");
	device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 1), NULL, "console");

	vty_init(&console_fops);
	return 0;
}

