#ifndef _ASM_GENERIC_TERMIOS_H
#define _ASM_GENERIC_TERMIOS_H


#include <linux/uaccess.h>
#include <asm/termbits.h>
#include <asm/ioctls.h>

/* Inlined from uapi/asm-generic/termios.h */
struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

/* struct termio, TIOCM_* modem control defines removed - unused */
/* end of uapi/asm-generic/termios.h */

#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"


#endif  
