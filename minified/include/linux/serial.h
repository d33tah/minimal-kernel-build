#ifndef _LINUX_SERIAL_H
#define _LINUX_SERIAL_H

#include <asm/page.h>
#include <linux/types.h>
#include <linux/tty_flags.h>

/* From uapi/linux/serial.h - reduced to only used values */
#define ASYNC_CLOSING_WAIT_NONE	65535

/* serial_icounter_struct, UART_LCR_WLEN, async_icount,
   ASYNC_CLOSING_WAIT_INF, SERIAL_XMIT_SIZE removed - unused */

#include <linux/compiler.h>

#endif  
