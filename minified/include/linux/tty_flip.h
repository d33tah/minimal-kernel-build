#ifndef _LINUX_TTY_FLIP_H
#define _LINUX_TTY_FLIP_H

#include <linux/tty_buffer.h>
#include <linux/tty_port.h>

struct tty_ldisc;

int tty_ldisc_receive_buf(struct tty_ldisc *ld, const unsigned char *p,
		const char *f, int count);


#endif  
