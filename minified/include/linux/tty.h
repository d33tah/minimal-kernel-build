/* --- 2026-02-12 --- Gutted: all TTY I/O paths dead.
 * struct tty_struct kept as minimal forward-declarable type. */
#ifndef _LINUX_TTY_H
#define _LINUX_TTY_H

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/tty_driver.h>
#include <linux/tty_buffer.h>
#include <linux/tty_port.h>
#include <linux/kref.h>

struct tty_struct {
	struct kref kref;
} __randomize_layout;

#endif
