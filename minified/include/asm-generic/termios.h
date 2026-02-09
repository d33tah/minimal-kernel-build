#ifndef _ASM_GENERIC_TERMIOS_H
#define _ASM_GENERIC_TERMIOS_H


#include <linux/uaccess.h>
/* --- Inlined uapi/asm-generic/termbits.h --- */

/* Inlined from termbits-common.h */
typedef unsigned char	cc_t;
typedef unsigned int	speed_t;

typedef unsigned int	tcflag_t;

#define NCCS 19
/* struct termios removed - only ktermios is used */

struct ktermios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
	speed_t c_ispeed;
	speed_t c_ospeed;
};

/* Inlined from uapi/asm-generic/termios.h */
struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};


#endif
