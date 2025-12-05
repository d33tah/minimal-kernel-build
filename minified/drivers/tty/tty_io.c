
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
#include <linux/devpts_fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/console.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/kd.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/ppp-ioctl.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/serial.h>
#include <linux/ratelimit.h>
#include <linux/compat.h>

#include <linux/uaccess.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>

#include <linux/kmod.h>
#include <linux/nsproxy.h>
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

static ssize_t tty_read(struct kiocb *, struct iov_iter *);
static ssize_t tty_write(struct kiocb *, struct iov_iter *);
static __poll_t tty_poll(struct file *, poll_table *);
static int tty_open(struct inode *, struct file *);
#define tty_compat_ioctl NULL
static int __tty_fasync(int fd, struct file *filp, int on);
static int tty_fasync(int fd, struct file *filp, int on);
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

static void tty_del_file(struct file *file)
{
	struct tty_file_private *priv = file->private_data;
	struct tty_struct *tty = priv->tty;

	spin_lock(&tty->files_lock);
	list_del(&priv->list);
	spin_unlock(&tty->files_lock);
	tty_free_file(file);
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
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_SLAVE &&
	    tty->link && tty->link->count)
		count++;
	if (tty_port_kopened(tty->port))
		kopen_count++;
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

/* Stub: tty_dev_name_to_number not used externally */
int tty_dev_name_to_number(const char *name, dev_t *number)
{
	return -ENODEV;
}

static ssize_t hung_up_tty_read(struct kiocb *iocb, struct iov_iter *to)
{
	return 0;
}

static ssize_t hung_up_tty_write(struct kiocb *iocb, struct iov_iter *from)
{
	return -EIO;
}

static __poll_t hung_up_tty_poll(struct file *filp, poll_table *wait)
{
	return EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDNORM | EPOLLWRNORM;
}

static long hung_up_tty_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	return cmd == TIOCSPGRP ? -ENOTTY : -EIO;
}

static long hung_up_tty_compat_ioctl(struct file *file,
				     unsigned int cmd, unsigned long arg)
{
	return cmd == TIOCSPGRP ? -ENOTTY : -EIO;
}

static int hung_up_tty_fasync(int fd, struct file *file, int on)
{
	return -ENOTTY;
}

static void tty_show_fdinfo(struct seq_file *m, struct file *file)
{
	/* Stub: TTY fdinfo not needed for minimal kernel */
}

static const struct file_operations tty_fops = {
	.llseek		= no_llseek,
	.read_iter	= tty_read,
	.write_iter	= tty_write,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.poll		= tty_poll,
	.unlocked_ioctl	= tty_ioctl,
	.compat_ioctl	= tty_compat_ioctl,
	.open		= tty_open,
	.release	= tty_release,
	.fasync		= tty_fasync,
	.show_fdinfo	= tty_show_fdinfo,
};

static const struct file_operations console_fops = {
	.llseek		= no_llseek,
	.read_iter	= tty_read,
	.write_iter	= redirected_tty_write,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.poll		= tty_poll,
	.unlocked_ioctl	= tty_ioctl,
	.compat_ioctl	= tty_compat_ioctl,
	.open		= tty_open,
	.release	= tty_release,
	.fasync		= tty_fasync,
};

static const struct file_operations hung_up_tty_fops = {
	.llseek		= no_llseek,
	.read_iter	= hung_up_tty_read,
	.write_iter	= hung_up_tty_write,
	.poll		= hung_up_tty_poll,
	.unlocked_ioctl	= hung_up_tty_ioctl,
	.compat_ioctl	= hung_up_tty_compat_ioctl,
	.release	= tty_release,
	.fasync		= hung_up_tty_fasync,
};

static DEFINE_SPINLOCK(redirect_lock);
static struct file *redirect;

void tty_wakeup(struct tty_struct *tty)
{
	struct tty_ldisc *ld;

	if (test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags)) {
		ld = tty_ldisc_ref(tty);
		if (ld) {
			if (ld->ops->write_wakeup)
				ld->ops->write_wakeup(tty);
			tty_ldisc_deref(ld);
		}
	}
	wake_up_interruptible_poll(&tty->write_wait, EPOLLOUT);
}

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

void tty_hangup(struct tty_struct *tty)
{
	tty_debug_hangup(tty, "hangup\n");
	schedule_work(&tty->hangup_work);
}

void tty_vhangup(struct tty_struct *tty)
{
	/* Stub: tty_vhangup not used in minimal kernel */
}

/* Stub: tty_vhangup_self not used externally */
void tty_vhangup_self(void)
{
}

void tty_vhangup_session(struct tty_struct *tty)
{
}

int tty_hung_up_p(struct file *filp)
{
	return (filp && filp->f_op == &hung_up_tty_fops);
}

void __stop_tty(struct tty_struct *tty)
{
	if (tty->flow.stopped)
		return;
	tty->flow.stopped = true;
	if (tty->ops->stop)
		tty->ops->stop(tty);
}

void stop_tty(struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&tty->flow.lock, flags);
	__stop_tty(tty);
	spin_unlock_irqrestore(&tty->flow.lock, flags);
}

void __start_tty(struct tty_struct *tty)
{
	if (!tty->flow.stopped || tty->flow.tco_stopped)
		return;
	tty->flow.stopped = false;
	if (tty->ops->start)
		tty->ops->start(tty);
	tty_wakeup(tty);
}

void start_tty(struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&tty->flow.lock, flags);
	__start_tty(tty);
	spin_unlock_irqrestore(&tty->flow.lock, flags);
}

static void tty_update_time(struct timespec64 *time)
{
	time64_t sec = ktime_get_real_seconds();

	
	if ((sec ^ time->tv_sec) & ~7)
		time->tv_sec = sec;
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
		size = ld->ops->read(tty, file, kernel_buf, size, &cookie, offset);
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
	struct inode *inode = file_inode(file);
	struct tty_struct *tty = file_tty(file);
	struct tty_ldisc *ld;

	if (tty_paranoia_check(tty, inode, "tty_read"))
		return -EIO;
	if (!tty || tty_io_error(tty))
		return -EIO;

	
	ld = tty_ldisc_ref_wait(tty);
	if (!ld)
		return hung_up_tty_read(iocb, to);
	i = -EIO;
	if (ld->ops->read)
		i = iterate_tty_read(ld, tty, file, to);
	tty_ldisc_deref(ld);

	if (i > 0)
		tty_update_time(&inode->i_atime);

	return i;
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
out:
	tty_write_unlock(tty);
	return ret;
}

void tty_write_message(struct tty_struct *tty, char *msg)
{
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

int tty_send_xchar(struct tty_struct *tty, char ch)
{
	return 0;
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

static void tty_driver_remove_tty(struct tty_driver *driver, struct tty_struct *tty)
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

	if (test_bit(TTY_EXCLUSIVE, &tty->flags) && !capable(CAP_SYS_ADMIN))
		return -EBUSY;

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

	

	if (!try_module_get(driver->owner))
		return ERR_PTR(-ENODEV);

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

void tty_save_termios(struct tty_struct *tty)
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
	flush_work(&tty->SAK_work);
	flush_work(&tty->hangup_work);
	if (tty->link) {
		flush_work(&tty->link->SAK_work);
		flush_work(&tty->link->hangup_work);
	}
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

static int tty_release_checks(struct tty_struct *tty, int idx)
{
#ifdef TTY_PARANOIA_CHECK
	if (idx < 0 || idx >= tty->driver->num) {
		tty_debug(tty, "bad idx %d\n", idx);
		return -1;
	}

	
	if (tty->driver->flags & TTY_DRIVER_DEVPTS_MEM)
		return 0;

	if (tty != tty->driver->ttys[idx]) {
		tty_debug(tty, "bad driver table[%d] = %p\n",
			  idx, tty->driver->ttys[idx]);
		return -1;
	}
	if (tty->driver->other) {
		struct tty_struct *o_tty = tty->link;

		if (o_tty != tty->driver->other->ttys[idx]) {
			tty_debug(tty, "bad other table[%d] = %p\n",
				  idx, tty->driver->other->ttys[idx]);
			return -1;
		}
		if (o_tty->link != tty) {
			tty_debug(tty, "bad link = %p\n", o_tty->link);
			return -1;
		}
	}
#endif
	return 0;
}

/* Stub: tty_kclose not used externally */
void tty_kclose(struct tty_struct *tty)
{
}

void tty_release_struct(struct tty_struct *tty, int idx)
{
	
	tty_ldisc_release(tty);

	
	tty_flush_works(tty);

	tty_debug_hangup(tty, "freeing structure\n");
	
	mutex_lock(&tty_mutex);
	release_tty(tty, idx);
	mutex_unlock(&tty_mutex);
}

int tty_release(struct inode *inode, struct file *filp)
{
	/* Minimal stub: simplified TTY release */
	struct tty_struct *tty = file_tty(filp);
	int idx;

	if (tty_paranoia_check(tty, inode, __func__))
		return 0;

	tty_lock(tty);
	idx = tty->index;

	if (tty_release_checks(tty, idx)) {
		tty_unlock(tty);
		return 0;
	}

	if (tty->ops->close)
		tty->ops->close(tty, filp);

	if (--tty->count < 0)
		tty->count = 0;

	tty_del_file(filp);
	tty_unlock(tty);

	if (!tty->count)
		tty_release_struct(tty, idx);

	return 0;
}

static struct tty_struct *tty_open_current_tty(dev_t device, struct file *filp)
{
	struct tty_struct *tty;
	int retval;

	if (device != MKDEV(TTYAUX_MAJOR, 0))
		return NULL;

	tty = get_current_tty();
	if (!tty)
		return ERR_PTR(-ENXIO);

	filp->f_flags |= O_NONBLOCK; 
	
	tty_lock(tty);
	tty_kref_put(tty);	

	retval = tty_reopen(tty);
	if (retval < 0) {
		tty_unlock(tty);
		tty = ERR_PTR(retval);
	}
	return tty;
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

/* STUB: tty_kopen_exclusive not used externally */
struct tty_struct *tty_kopen_exclusive(dev_t device) { return ERR_PTR(-EINVAL); }

/* STUB: tty_kopen_shared not used externally */
struct tty_struct *tty_kopen_shared(dev_t device) { return ERR_PTR(-EINVAL); }

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
	int noctty, retval;
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
		
		if (tty_hung_up_p(filp))
			filp->f_op = &tty_fops;
		goto retry_open;
	}
	clear_bit(TTY_HUPPED, &tty->flags);

	noctty = (filp->f_flags & O_NOCTTY) ||
		 (IS_ENABLED(CONFIG_VT) && device == MKDEV(TTY_MAJOR, 0)) ||
		 device == MKDEV(TTYAUX_MAJOR, 1) ||
		 (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
		  tty->driver->subtype == PTY_TYPE_MASTER);
	if (!noctty)
		tty_open_proc_set_tty(filp, tty);
	tty_unlock(tty);
	return 0;
}

static __poll_t tty_poll(struct file *filp, poll_table *wait)
{
	struct tty_struct *tty = file_tty(filp);
	struct tty_ldisc *ld;
	__poll_t ret = 0;

	if (tty_paranoia_check(tty, file_inode(filp), "tty_poll"))
		return 0;

	ld = tty_ldisc_ref_wait(tty);
	if (!ld)
		return hung_up_tty_poll(filp, wait);
	if (ld->ops->poll)
		ret = ld->ops->poll(tty, filp, wait);
	tty_ldisc_deref(ld);
	return ret;
}

static int __tty_fasync(int fd, struct file *filp, int on)
{
	struct tty_struct *tty = file_tty(filp);
	unsigned long flags;
	int retval = 0;

	if (tty_paranoia_check(tty, file_inode(filp), "tty_fasync"))
		goto out;

	retval = fasync_helper(fd, filp, on, &tty->fasync);
	if (retval <= 0)
		goto out;

	if (on) {
		enum pid_type type;
		struct pid *pid;

		spin_lock_irqsave(&tty->ctrl.lock, flags);
		if (tty->ctrl.pgrp) {
			pid = tty->ctrl.pgrp;
			type = PIDTYPE_PGID;
		} else {
			pid = task_pid(current);
			type = PIDTYPE_TGID;
		}
		get_pid(pid);
		spin_unlock_irqrestore(&tty->ctrl.lock, flags);
		__f_setown(filp, pid, type, 0);
		put_pid(pid);
		retval = 0;
	}
out:
	return retval;
}

static int tty_fasync(int fd, struct file *filp, int on)
{
	struct tty_struct *tty = file_tty(filp);
	int retval = -ENOTTY;

	tty_lock(tty);
	if (!tty_hung_up_p(filp))
		retval = __tty_fasync(fd, filp, on);
	tty_unlock(tty);

	return retval;
}

static int tiocgwinsz(struct tty_struct *tty, struct winsize __user *arg)
{
	int err;

	mutex_lock(&tty->winsize_mutex);
	err = copy_to_user(arg, &tty->winsize, sizeof(*arg));
	mutex_unlock(&tty->winsize_mutex);

	return err ? -EFAULT : 0;
}

int tty_do_resize(struct tty_struct *tty, struct winsize *ws)
{
	struct pid *pgrp;

	
	mutex_lock(&tty->winsize_mutex);
	if (!memcmp(ws, &tty->winsize, sizeof(*ws)))
		goto done;

	
	pgrp = tty_get_pgrp(tty);
	if (pgrp)
		kill_pgrp(pgrp, SIGWINCH, 1);
	put_pid(pgrp);

	tty->winsize = *ws;
done:
	mutex_unlock(&tty->winsize_mutex);
	return 0;
}

static int tiocswinsz(struct tty_struct *tty, struct winsize __user *arg)
{
	struct winsize tmp_ws;

	if (copy_from_user(&tmp_ws, arg, sizeof(*arg)))
		return -EFAULT;

	if (tty->ops->resize)
		return tty->ops->resize(tty, &tmp_ws);
	else
		return tty_do_resize(tty, &tmp_ws);
}

/* Stub: tty_get_icount not used externally */
int tty_get_icount(struct tty_struct *tty,
		   struct serial_icounter_struct *icount)
{
	return -ENOTTY;
}

static struct tty_struct *tty_pair_get_tty(struct tty_struct *tty)
{
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_MASTER)
		tty = tty->link;
	return tty;
}

long tty_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* Minimal stub: handle only essential ioctl operations */
	struct tty_struct *tty = file_tty(file);
	struct tty_struct *real_tty;
	void __user *p = (void __user *)arg;
	int retval;

	if (tty_paranoia_check(tty, file_inode(file), "tty_ioctl"))
		return -EINVAL;

	real_tty = tty_pair_get_tty(tty);

	/* Handle minimal set of ioctls needed for basic console */
	switch (cmd) {
	case TIOCGWINSZ:
		return tiocgwinsz(real_tty, p);
	case TIOCSWINSZ:
		return tiocswinsz(real_tty, p);
	default:
		break;
	}

	/* Delegate to driver-specific ioctl if available */
	if (tty->ops->ioctl) {
		retval = tty->ops->ioctl(tty, cmd, arg);
		if (retval != -ENOIOCTLCMD)
			return retval;
	}

	return -ENOTTY;
}

void __do_SAK(struct tty_struct *tty)
{
	
}

static void do_SAK_work(struct work_struct *work)
{
	struct tty_struct *tty =
		container_of(work, struct tty_struct, SAK_work);
	__do_SAK(tty);
}

/* Stubbed: do_SAK not used externally */
void do_SAK(struct tty_struct *tty)
{
}

static struct device *tty_get_device(struct tty_struct *tty)
{
	dev_t devt = tty_devnum(tty);

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
	mutex_init(&tty->throttle_mutex);
	init_rwsem(&tty->termios_rwsem);
	mutex_init(&tty->winsize_mutex);
	init_ldsem(&tty->ldisc_sem);
	init_waitqueue_head(&tty->write_wait);
	init_waitqueue_head(&tty->read_wait);
	INIT_WORK(&tty->hangup_work, do_tty_hangup);
	mutex_init(&tty->atomic_write_lock);
	spin_lock_init(&tty->ctrl.lock);
	spin_lock_init(&tty->flow.lock);
	spin_lock_init(&tty->files_lock);
	INIT_LIST_HEAD(&tty->tty_files);
	INIT_WORK(&tty->SAK_work, do_SAK_work);

	tty->driver = driver;
	tty->ops = driver->ops;
	tty->index = idx;
	tty_line_name(driver, idx, tty->name);
	tty->dev = tty_get_device(tty);

	return tty;
}

int tty_put_char(struct tty_struct *tty, unsigned char ch)
{
	if (tty->ops->put_char)
		return tty->ops->put_char(tty, ch);
	return tty->ops->write(tty, &ch, 1);
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
	return tty_register_device_attr(driver, index, device, NULL, NULL);
}

static void tty_device_create_release(struct device *dev)
{
	dev_dbg(dev, "releasing...\n");
	kfree(dev);
}

struct device *tty_register_device_attr(struct tty_driver *driver,
				   unsigned index, struct device *device,
				   void *drvdata,
				   const struct attribute_group **attr_grp)
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
	dev->groups = attr_grp;
	dev_set_drvdata(dev, drvdata);

	dev_set_uevent_suppress(dev, 1);

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

	dev_set_uevent_suppress(dev, 0);
	kobject_uevent(&dev->kobj, KOBJ_ADD);

	return dev;

err_del:
	device_del(dev);
err_put:
	put_device(dev);

	return ERR_PTR(retval);
}

void tty_unregister_device(struct tty_driver *driver, unsigned index)
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
		driver->ttys = kcalloc(lines, sizeof(*driver->ttys),
				GFP_KERNEL);
		driver->termios = kcalloc(lines, sizeof(*driver->termios),
				GFP_KERNEL);
		if (!driver->ttys || !driver->termios) {
			err = -ENOMEM;
			goto err_free_all;
		}
	}

	if (!(flags & TTY_DRIVER_DYNAMIC_ALLOC)) {
		driver->ports = kcalloc(lines, sizeof(*driver->ports),
				GFP_KERNEL);
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
		proc_tty_unregister_driver(driver);
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
	proc_tty_register_driver(driver);
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

/* STUB: tty_unregister_driver not used in minimal kernel */
void tty_unregister_driver(struct tty_driver *driver) { }

dev_t tty_devnum(struct tty_struct *tty)
{
	return MKDEV(tty->driver->major, tty->driver->minor_start) + tty->index;
}

void tty_default_fops(struct file_operations *fops)
{
	*fops = tty_fops;
}

static char *tty_devnode(struct device *dev, umode_t *mode)
{
	if (!mode)
		return NULL;
	if (dev->devt == MKDEV(TTYAUX_MAJOR, 0) ||
	    dev->devt == MKDEV(TTYAUX_MAJOR, 2))
		*mode = 0666;
	return NULL;
}

static int __init tty_class_init(void)
{
	tty_class = class_create(THIS_MODULE, "tty");
	if (IS_ERR(tty_class))
		return PTR_ERR(tty_class);
	tty_class->devnode = tty_devnode;
	return 0;
}

postcore_initcall(tty_class_init);

static struct cdev tty_cdev, console_cdev;

static ssize_t show_cons_active(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	/* Stub: console listing not needed for minimal kernel */
	return 0;
}
static DEVICE_ATTR(active, S_IRUGO, show_cons_active, NULL);

static struct attribute *cons_dev_attrs[] = {
	&dev_attr_active.attr,
	NULL
};

ATTRIBUTE_GROUPS(cons_dev);

static struct device *consdev;

void console_sysfs_notify(void)
{
	if (consdev)
		sysfs_notify(&consdev->kobj, NULL, "active");
}

int __init tty_init(void)
{
	tty_sysctl_init();
	cdev_init(&tty_cdev, &tty_fops);
	if (cdev_add(&tty_cdev, MKDEV(TTYAUX_MAJOR, 0), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 0), 1, "/dev/tty") < 0)
		panic("Couldn't register /dev/tty driver\n");
	device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 0), NULL, "tty");

	cdev_init(&console_cdev, &console_fops);
	if (cdev_add(&console_cdev, MKDEV(TTYAUX_MAJOR, 1), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 1), 1, "/dev/console") < 0)
		panic("Couldn't register /dev/console driver\n");
	consdev = device_create_with_groups(tty_class, NULL,
					    MKDEV(TTYAUX_MAJOR, 1), NULL,
					    cons_dev_groups, "console");
	if (IS_ERR(consdev))
		consdev = NULL;

	vty_init(&console_fops);
	return 0;
}

