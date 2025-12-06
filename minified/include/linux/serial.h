#ifndef _LINUX_SERIAL_H
#define _LINUX_SERIAL_H

#include <asm/page.h>
#include <linux/types.h>
#include <linux/tty_flags.h>

/* From uapi/linux/serial.h - inlined */
#define ASYNC_CLOSING_WAIT_INF	0
#define ASYNC_CLOSING_WAIT_NONE	65535

struct serial_icounter_struct {
	int cts, dsr, rng, dcd;
	int rx, tx;
	int frame, overrun, parity, brk;
	int buf_overrun;
	int reserved[9];
};

#define UART_LCR_WLEN(x)	((x) - 5)


struct async_icount {
	__u32	cts, dsr, rng, dcd, tx, rx;
	__u32	frame, parity, overrun, brk;
	__u32	buf_overrun;
};

#define SERIAL_XMIT_SIZE PAGE_SIZE

#include <linux/compiler.h>

#endif  
