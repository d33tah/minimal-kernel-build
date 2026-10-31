
#include <linux/tty.h>
#include <linux/file.h>
#include <linux/console.h>
#include <linux/mm.h>
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

	nonseekable_open(inode, filp);

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

	driver->num = lines;
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
