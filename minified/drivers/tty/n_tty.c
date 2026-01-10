
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
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ratelimit.h>
#include <linux/vmalloc.h>
#include "tty.h"

/* WAKEUP_CHARS removed - unused */

struct n_tty_data {
	/* read_head, read_tail, read_buf removed - n_tty_read is stub that returns 0 */
	/* atomic_read_lock removed - only initialized, never locked */
	struct mutex output_lock;
};

static void n_tty_write_wakeup(struct tty_struct *tty)
{
	/* kill_fasync removed - empty stub */
}

/* n_tty_flush_buffer, n_tty_set_termios removed - ldisc ops never called */

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

	/* atomic_read_lock init removed - field removed */
	mutex_init(&ldata->output_lock);
	tty->disc_data = ldata;

	return 0;
}

static ssize_t n_tty_read(struct tty_struct *tty, struct file *file,
			  unsigned char *buf, size_t nr, void **cookie,
			  unsigned long offset)
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

	/* tty_check_change always returns 0 - removed dead code */

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

/* n_tty_poll simplified - poll syscall returns -ENOSYS directly */
static __poll_t n_tty_poll(struct tty_struct *tty, struct file *file,
			   poll_table *wait)
{
	return 0;
}

/* n_tty_ioctl simplified - ioctl syscall returns -ENOTTY directly */
static int n_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
		       unsigned long arg)
{
	return -ENOTTY;
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
	.owner = THIS_MODULE,
	.num = N_TTY,
	.name = "n_tty",
	.open = n_tty_open,
	.close = n_tty_close,
	/* .flush_buffer, .set_termios removed - ldisc ops never called */
	.read = n_tty_read,
	.write = n_tty_write,
	.ioctl = n_tty_ioctl,
	.poll = n_tty_poll,
	.receive_buf = n_tty_receive_buf,
	.write_wakeup = n_tty_write_wakeup,
	.receive_buf2 = n_tty_receive_buf2,
};

void __init n_tty_init(void)
{
	tty_register_ldisc(&n_tty_ops);
}
