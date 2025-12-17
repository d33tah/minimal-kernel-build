/* Minimal includes for tty_mutex */
#include <linux/tty.h>
#include "tty.h"



void tty_lock(struct tty_struct *tty)
{
	if (WARN(tty->magic != TTY_MAGIC, "L Bad %p\n", tty))
		return;
	tty_kref_get(tty);
	mutex_lock(&tty->legacy_mutex);
}

int tty_lock_interruptible(struct tty_struct *tty)
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

void tty_unlock(struct tty_struct *tty)
{
	if (WARN(tty->magic != TTY_MAGIC, "U Bad %p\n", tty))
		return;
	mutex_unlock(&tty->legacy_mutex);
	tty_kref_put(tty);
}
