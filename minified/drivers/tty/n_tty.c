
#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
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

struct n_tty_data {
	/* read_head, read_tail, read_buf removed - n_tty_read is stub that returns 0 */
	/* atomic_read_lock removed - only initialized, never locked */
	struct mutex output_lock;
};

/* n_tty_write_wakeup removed - write_wakeup callback never invoked */

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

/* n_tty_poll removed - poll/select syscalls return ENOSYS */
/* n_tty_ioctl removed - ld->ops->ioctl never called */
/* n_tty_receive_buf, n_tty_receive_buf2 removed - ops->receive_buf* never called */

static struct tty_ldisc_ops n_tty_ops = {
	.owner = THIS_MODULE,
	.num = N_TTY,
	.name = "n_tty",
	.open = n_tty_open,
	.close = n_tty_close,
	.read = n_tty_read,
	.write = n_tty_write,
	/* .receive_buf, .receive_buf2, .write_wakeup removed - never called */
};

void __init n_tty_init(void)
{
	tty_register_ldisc(&n_tty_ops);
}
