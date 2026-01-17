#ifndef _LINUX_TTY_FLIP_H
#define _LINUX_TTY_FLIP_H

#include <linux/tty_buffer.h>
#include <linux/tty_port.h>

/* tty_buffer_space_avail, tty_buffer_request_room, tty_insert_flip_string_*,
 * tty_prepare_flip_string, tty_flip_buffer_push, __tty_insert_flip_char,
 * tty_ldisc_receive_buf all removed - never called (tty input buffering unused) */

#endif
