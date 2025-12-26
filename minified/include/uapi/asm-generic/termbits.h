#ifndef __ASM_GENERIC_TERMBITS_H
#define __ASM_GENERIC_TERMBITS_H

/* Inlined from termbits-common.h */
typedef unsigned char	cc_t;
typedef unsigned int	speed_t;

/* Only keeping flags that are actually used in TTY code */
#define ICRNL	0x100
#define IXON	0x0400

#define OPOST	0x01

/* Only keeping B0 and B38400 which are actually used */
#define     B0		0x00000000
#define B38400		0x0000000f

#define IBSHIFT		16

typedef unsigned int	tcflag_t;

#define NCCS 19
struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
};

struct termios2 {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
	speed_t c_ispeed;
	speed_t c_ospeed;
};

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

/* c_iflag - only used ones kept */
#define IUTF8	0x4000

/* c_oflag - only used ones kept */
#define ONLCR	0x00004

/* c_cflag defines - kept those actually used */
#define CBAUD		0x0000100f
#define   CS8		0x00000030
#define CREAD		0x00000080
#define HUPCL		0x00000400
#define CLOCAL		0x00000800
#define CBAUDEX		0x00001000
#define BOTHER		0x00001000

/* c_lflag - only used ones kept */
#define ISIG	0x00001
#define ICANON	0x00002
#define ECHO	0x00008
#define ECHOE	0x00010
#define ECHOK	0x00020
#define TOSTOP	0x00100
#define ECHOCTL	0x00200
#define ECHOKE	0x00800
#define IEXTEN	0x08000

#endif
