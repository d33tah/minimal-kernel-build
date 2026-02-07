/* --- 2026-02-06 22:40 --- Gutted: most declarations dead after tty_io.c removal */

#ifndef _TTY_INTERNAL_H
#define _TTY_INTERNAL_H

void tty_buffer_init(struct tty_port *port);
/* tty_buffer_cancel_work removed - never called */

#endif
