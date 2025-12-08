#ifndef __ASM_GENERIC_TERMBITS_H
#define __ASM_GENERIC_TERMBITS_H

/* Inlined from termbits-common.h */
typedef unsigned char	cc_t;
typedef unsigned int	speed_t;

#define IGNBRK	0x001
#define BRKINT	0x002
#define IGNPAR	0x004
#define PARMRK	0x008
#define INPCK	0x010
#define ISTRIP	0x020
#define INLCR	0x040
#define IGNCR	0x080
#define ICRNL	0x100
#define IXANY	0x800

#define OPOST	0x01
#define OCRNL	0x08
#define ONOCR	0x10
#define ONLRET	0x20
#define OFILL	0x40
#define OFDEL	0x80

/* Only keeping B0 and B38400 which are actually used */
#define     B0		0x00000000
#define B38400		0x0000000f

#define CMSPAR		0x40000000
#define CRTSCTS		0x80000000

#define IBSHIFT		16

#define TCOOFF		0
#define TCOON		1
#define TCIOFF		2
#define TCION		3

#define TCIFLUSH	0
#define TCOFLUSH	1
#define TCIOFLUSH	2
/* End of termbits-common.h */

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

#define VINTR		 0
#define VQUIT		 1
#define VERASE		 2
#define VKILL		 3
#define VEOF		 4
#define VTIME		 5
#define VMIN		 6
#define VSWTC		 7
#define VSTART		 8
#define VSTOP		 9
#define VSUSP		10
#define VEOL		11
#define VREPRINT	12
#define VDISCARD	13
#define VWERASE		14
#define VLNEXT		15
#define VEOL2		16

#define IUCLC	0x0200
#define IXON	0x0400
#define IXOFF	0x1000
#define IMAXBEL	0x2000
#define IUTF8	0x4000

/* Removed delay flags (NLDLY, CRDLY, TABDLY, BSDLY, VTDLY, FFDLY) - unused */
#define OLCUC	0x00002
#define ONLCR	0x00004

/* c_cflag defines - kept those actually used */
#define CBAUD		0x0000100f
#define CSIZE		0x00000030
#define   CS5		0x00000000
#define   CS6		0x00000010
#define   CS7		0x00000020
#define   CS8		0x00000030
#define CSTOPB		0x00000040
#define CREAD		0x00000080
#define PARENB		0x00000100
#define HUPCL		0x00000400
#define CLOCAL		0x00000800
#define CBAUDEX		0x00001000
#define BOTHER		0x00001000	 

#define ISIG	0x00001
#define ICANON	0x00002
#define XCASE	0x00004
#define ECHO	0x00008
#define ECHOE	0x00010
#define ECHOK	0x00020
#define ECHONL	0x00040
#define NOFLSH	0x00080
#define TOSTOP	0x00100
#define ECHOCTL	0x00200
#define ECHOPRT	0x00400
#define ECHOKE	0x00800
#define FLUSHO	0x01000
#define PENDIN	0x04000
#define IEXTEN	0x08000
#define EXTPROC	0x10000

#define	TCSANOW		0
#define	TCSADRAIN	1
#define	TCSAFLUSH	2

#endif  
