/* Minimal serial.h - only keep what's actually used */
#ifndef _UAPI_LINUX_SERIAL_H
#define _UAPI_LINUX_SERIAL_H

#include <linux/types.h>
#include <linux/tty_flags.h>

/* TTY port constants */
#define ASYNC_CLOSING_WAIT_INF	0
#define ASYNC_CLOSING_WAIT_NONE	65535

/* Only struct used in tty_io.c */
struct serial_icounter_struct {
	int cts, dsr, rng, dcd;
	int rx, tx;
	int frame, overrun, parity, brk;
	int buf_overrun;
	int reserved[9];
};

#endif
