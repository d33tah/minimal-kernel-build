 
#ifndef _LINUX_SERIAL_H
#define _LINUX_SERIAL_H

#include <asm/page.h>
#include <uapi/linux/serial.h>

 
#define UART_LCR_WLEN(x)	((x) - 5)

 

struct async_icount {
	__u32	cts, dsr, rng, dcd, tx, rx;
	__u32	frame, parity, overrun, brk;
	__u32	buf_overrun;
};

 
#define SERIAL_XMIT_SIZE PAGE_SIZE

#include <linux/compiler.h>

#endif  
