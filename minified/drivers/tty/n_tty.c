 

#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/bitops.h>
#include <linux/audit.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ratelimit.h>
#include <linux/vmalloc.h>
#include "tty.h"

#define WAKEUP_CHARS 256

struct n_tty_data {
	size_t read_head;
	size_t read_tail;
	struct mutex atomic_read_lock;
	struct mutex output_lock;
	char read_buf[N_TTY_BUF_SIZE];
};

 
static void n_tty_write_wakeup(struct tty_struct *tty)
{
	if (tty->fasync && test_and_clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags))
		kill_fasync(&tty->fasync, SIGIO, POLL_OUT);
}

 
static void n_tty_flush_buffer(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (ldata) {
		ldata->read_head = ldata->read_tail = 0;
	}
	wake_up_interruptible(&tty->read_wait);
	if (tty->link)
		n_tty_flush_buffer(tty->link);
}

 
static void n_tty_set_termios(struct tty_struct *tty, struct ktermios *old)
{
	 
}

 
static void n_tty_close(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (ldata) {
		kfree(ldata);
		tty->disc_data = NULL;
	}
}

 
static int n_tty_open(struct tty_struct *tty)
{
	struct n_tty_data *ldata;

	ldata = kzalloc(sizeof(*ldata), GFP_KERNEL);
	if (!ldata)
		return -ENOMEM;

	mutex_init(&ldata->atomic_read_lock);
	mutex_init(&ldata->output_lock);
	tty->disc_data = ldata;

	return 0;
}

 
static ssize_t n_tty_read(struct tty_struct *tty, struct file *file,
			 unsigned char *buf, size_t nr,
			 void **cookie, unsigned long offset)
{
	 
	return 0;
}

 
static ssize_t n_tty_write(struct tty_struct *tty, struct file *file,
			   const unsigned char *buf, size_t nr)
{
	struct n_tty_data *ldata = tty->disc_data;
	const unsigned char *b = buf;
	ssize_t retval = 0;
	int c;

	if (L_TOSTOP(tty) && file->f_op->write_iter != redirected_tty_write) {
		retval = tty_check_change(tty);
		if (retval)
			return retval;
	}

	down_read(&tty->termios_rwsem);

	 
	while (nr > 0) {
		mutex_lock(&ldata->output_lock);
		c = tty->ops->write(tty, b, nr);
		mutex_unlock(&ldata->output_lock);

		if (c < 0) {
			retval = c;
			break;
		}
		if (!c)
			break;

		b += c;
		nr -= c;
	}

	if (tty->ops->flush_chars)
		tty->ops->flush_chars(tty);

	up_read(&tty->termios_rwsem);

	if (b - buf)
		retval = b - buf;

	return retval;
}

 
static __poll_t n_tty_poll(struct tty_struct *tty, struct file *file,
			  poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(file, &tty->read_wait, wait);
	poll_wait(file, &tty->write_wait, wait);

	 
	mask |= EPOLLOUT | EPOLLWRNORM;

	return mask;
}

 
static int n_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
		       unsigned long arg)
{
	switch (cmd) {
	case TIOCOUTQ:
		return put_user(tty_chars_in_buffer(tty), (int __user *) arg);
	case TIOCINQ:
		return put_user(0, (unsigned int __user *) arg);
	default:
		return n_tty_ioctl_helper(tty, cmd, arg);
	}
}

 
static void n_tty_receive_buf(struct tty_struct *tty, const unsigned char *cp,
			      const char *fp, int count)
{
	 
}

 
static int n_tty_receive_buf2(struct tty_struct *tty, const unsigned char *cp,
			      const char *fp, int count)
{
	 
	return 0;
}

static struct tty_ldisc_ops n_tty_ops = {
	.owner		 = THIS_MODULE,
	.num		 = N_TTY,
	.name            = "n_tty",
	.open            = n_tty_open,
	.close           = n_tty_close,
	.flush_buffer    = n_tty_flush_buffer,
	.read            = n_tty_read,
	.write           = n_tty_write,
	.ioctl           = n_tty_ioctl,
	.set_termios     = n_tty_set_termios,
	.poll            = n_tty_poll,
	.receive_buf     = n_tty_receive_buf,
	.write_wakeup    = n_tty_write_wakeup,
	.receive_buf2	 = n_tty_receive_buf2,
};

/* Stub: n_tty_inherit_ops not called externally */
void n_tty_inherit_ops(struct tty_ldisc_ops *ops) { }

void __init n_tty_init(void)
{
	tty_register_ldisc(&n_tty_ops);
}
