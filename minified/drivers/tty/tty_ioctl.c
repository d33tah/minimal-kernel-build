 
 

#include <linux/types.h>
#include <linux/termios.h>
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

 
unsigned int tty_write_room(struct tty_struct *tty)
{
	if (tty->ops->write_room)
		return tty->ops->write_room(tty);
	return 2048;
}

 
void tty_driver_flush_buffer(struct tty_struct *tty)
{
	if (tty->ops->flush_buffer)
		tty->ops->flush_buffer(tty);
}

 
void tty_unthrottle(struct tty_struct *tty)
{
	down_write(&tty->termios_rwsem);
	if (test_and_clear_bit(TTY_THROTTLED, &tty->flags) &&
	    tty->ops->unthrottle)
		tty->ops->unthrottle(tty);
	tty->flow_change = 0;
	up_write(&tty->termios_rwsem);
}

 
int tty_throttle_safe(struct tty_struct *tty)
{
	int ret = 0;
	mutex_lock(&tty->throttle_mutex);
	if (!tty_throttled(tty)) {
		if (tty->flow_change != TTY_THROTTLE_SAFE)
			ret = 1;
		else {
			set_bit(TTY_THROTTLED, &tty->flags);
			if (tty->ops->throttle)
				tty->ops->throttle(tty);
		}
	}
	mutex_unlock(&tty->throttle_mutex);
	return ret;
}

 
int tty_unthrottle_safe(struct tty_struct *tty)
{
	int ret = 0;
	mutex_lock(&tty->throttle_mutex);
	if (tty_throttled(tty)) {
		if (tty->flow_change != TTY_UNTHROTTLE_SAFE)
			ret = 1;
		else {
			clear_bit(TTY_THROTTLED, &tty->flags);
			if (tty->ops->unthrottle)
				tty->ops->unthrottle(tty);
		}
	}
	mutex_unlock(&tty->throttle_mutex);
	return ret;
}

 
void tty_wait_until_sent(struct tty_struct *tty, long timeout)
{
	 
}

 
void tty_termios_copy_hw(struct ktermios *new, struct ktermios *old)
{
	new->c_cflag &= HUPCL | CREAD | CLOCAL;
	new->c_cflag |= old->c_cflag & ~(HUPCL | CREAD | CLOCAL);
	new->c_ispeed = old->c_ispeed;
	new->c_ospeed = old->c_ospeed;
}

 
int tty_termios_hw_change(const struct ktermios *a, const struct ktermios *b)
{
	if (a->c_ispeed != b->c_ispeed || a->c_ospeed != b->c_ospeed)
		return 1;
	if ((a->c_cflag ^ b->c_cflag) & ~(HUPCL | CREAD | CLOCAL))
		return 1;
	return 0;
}

 
unsigned char tty_get_char_size(unsigned int cflag)
{
	switch (cflag & CSIZE) {
	case CS5: return 5;
	case CS6: return 6;
	case CS7: return 7;
	case CS8:
	default:  return 8;
	}
}

 
unsigned char tty_get_frame_size(unsigned int cflag)
{
	unsigned char bits = 2 + tty_get_char_size(cflag);
	if (cflag & CSTOPB) bits++;
	if (cflag & PARENB) bits++;
	return bits;
}

 
int tty_set_termios(struct tty_struct *tty, struct ktermios *new_termios)
{
	struct ktermios old_termios;
	struct tty_ldisc *ld;

	down_write(&tty->termios_rwsem);
	old_termios = tty->termios;
	tty->termios = *new_termios;

	if (tty->ops->set_termios)
		tty->ops->set_termios(tty, &old_termios);

	ld = tty_ldisc_ref(tty);
	if (ld) {
		if (ld->ops->set_termios)
			ld->ops->set_termios(tty, &old_termios);
		tty_ldisc_deref(ld);
	}
	up_write(&tty->termios_rwsem);
	return 0;
}

 
int tty_mode_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	 
	return -ENOIOCTLCMD;
}

 
int tty_perform_flush(struct tty_struct *tty, unsigned long arg)
{
	int retval = tty_check_change(tty);
	if (retval)
		return retval;

	 
	if (arg == TCOFLUSH || arg == TCIOFLUSH)
		tty_driver_flush_buffer(tty);

	return 0;
}

 
int n_tty_ioctl_helper(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	 
	return tty_mode_ioctl(tty, cmd, arg);
}
