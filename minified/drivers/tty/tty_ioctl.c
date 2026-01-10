/* tty_ioctl.c - Minimal stubs
   Most functions removed - ioctl syscall returns -ENOTTY directly */

#include <linux/tty.h>

unsigned int tty_chars_in_buffer(struct tty_struct *tty)
{
	return 0;
}

unsigned int tty_write_room(struct tty_struct *tty)
{
	return 2048;
}
