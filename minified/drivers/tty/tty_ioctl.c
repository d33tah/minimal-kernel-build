
#include <linux/types.h>
#include <linux/types.h>
#include <asm/termios.h>
#include <linux/errno.h>
#include <linux/sched/signal.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/tty.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/compat.h>
#include "tty.h"

#include <asm/io.h>
#include <linux/uaccess.h>

unsigned int tty_chars_in_buffer(struct tty_struct *tty)
{
	if (tty->ops->chars_in_buffer)
		return tty->ops->chars_in_buffer(tty);
	return 0;
}

int tty_mode_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{

	return -ENOIOCTLCMD;
}

int n_tty_ioctl_helper(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	 
	return tty_mode_ioctl(tty, cmd, arg);
}
