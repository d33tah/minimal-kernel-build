
#include <linux/tty.h>
#include <linux/file.h>
#include <linux/console.h>
#include <linux/mm.h>
#include <linux/uio.h>

#include <linux/vt_kern.h>

LIST_HEAD(tty_drivers);

DEFINE_MUTEX(tty_mutex);

static ssize_t tty_write(struct kiocb *, struct iov_iter *);
static int tty_open(struct inode *, struct file *);

static struct tty_driver *get_tty_driver(dev_t device, int *index)
{
	struct tty_driver *p;

	list_for_each_entry(p, &tty_drivers, tty_drivers) {
		dev_t base = MKDEV(p->major, p->minor_start);

		if (device < base || device >= base + p->num)
			continue;
		*index = device - base;
		return p;
	}
	return NULL;
}

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
		cond_resched();
	}
	if (written)
		ret = written;
out:
	mutex_unlock(&tty->atomic_write_lock);
	return ret;
}

/* Writes "<driver->name><index + name_base>", e.g. "tty1"; the decimal
   number is the only runtime part (index ranges over the driver's lines). */
static void tty_line_name(struct tty_driver *driver, int index, char *p)
{
	unsigned int n = index + driver->name_base, d = 1;

	strcpy(p, driver->name);
	p += strlen(p);
	while (n / d >= 10)
		d *= 10;
	for (; d; d /= 10)
		*p++ = '0' + n / d % 10;
	*p = '\0';
}

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
	tty->driver = driver;
	tty->ops = driver->ops;
	tty->index = idx;

	retval = driver->ops->install ? driver->ops->install(driver, tty) :
					tty_standard_install(driver, tty);
	if (retval < 0) {
		kfree(tty);
		return ERR_PTR(retval);
	}
	return tty;
}

static struct tty_driver *tty_lookup_driver(dev_t device, int *index)
{
	switch (device) {
	case MKDEV(TTY_MAJOR, 0): {
		extern struct tty_driver *console_driver;

		*index = 0;
		return console_driver;
	}
	case MKDEV(TTYAUX_MAJOR, 1):
		return console_device(index);
	default:
		return get_tty_driver(device, index);
	}
}

/*
 * /dev/tty (5:0) is the controlling terminal; no task here ever acquires one,
 * so opening it fails with -ENXIO. Every other node resolves to a driver line,
 * whose tty is created on first open and reused afterwards.
 */
static int tty_open(struct inode *inode, struct file *filp)
{
	struct tty_driver *driver;
	struct tty_struct *tty;
	int index = -1;

	nonseekable_open(inode, filp);

	if (inode->i_rdev == MKDEV(TTYAUX_MAJOR, 0))
		return -ENXIO;

	mutex_lock(&tty_mutex);
	driver = tty_lookup_driver(inode->i_rdev, &index);
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
struct device *tty_register_device(struct tty_driver *driver, unsigned index,
				   struct device *device)
{
	char name[64];
	dev_t devt = MKDEV(driver->major, driver->minor_start) + index;
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

	retval = tty_cdev_add(driver, devt, index, 1);
	if (retval)
		goto err_put;

	return dev;

err_put:
	put_device(dev);

	return ERR_PTR(retval);
}

/* Removed: tty_unregister_device + the device teardown chain
   (device_destroy/device_unregister/device_del/bus_remove_device) - no tty
   device is ever unregistered and no driver kref is dropped on this build, so
   the entire char-device teardown path was runtime-dead. */

struct tty_driver *tty_alloc_driver(unsigned int lines)
{
	struct tty_driver *driver;

	driver = kzalloc(sizeof(*driver), GFP_KERNEL);
	if (!driver)
		return ERR_PTR(-ENOMEM);

	driver->num = lines;
	driver->ttys = kcalloc(lines, sizeof(*driver->ttys), GFP_KERNEL);
	driver->cdevs = kcalloc(lines, sizeof(*driver->cdevs), GFP_KERNEL);
	if (!driver->ttys || !driver->cdevs) {
		kfree(driver->ttys);
		kfree(driver->cdevs);
		kfree(driver);
		return ERR_PTR(-ENOMEM);
	}
	return driver;
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
	return 0;

err_unreg_devs:
	mutex_lock(&tty_mutex);
	list_del(&driver->tty_drivers);
	mutex_unlock(&tty_mutex);

	unregister_chrdev_region(dev, driver->num);
err:
	return error;
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

	cdev_init(&console_cdev, &tty_fops);
	if (cdev_add(&console_cdev, MKDEV(TTYAUX_MAJOR, 1), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 1), 1, "/dev/console") <
		    0)
		panic("Couldn't register /dev/console driver\n");
	device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 1), NULL, "console");

	vty_init(&tty_fops);
	return 0;
}
