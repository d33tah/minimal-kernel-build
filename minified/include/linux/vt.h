#ifndef _LINUX_VT_H
#define _LINUX_VT_H

/* Inlined from uapi/linux/vt.h */
#define MIN_NR_CONSOLES 1
#define MAX_NR_CONSOLES	63

struct vt_mode {
	char mode;
	char waitv;
	short relsig;
	short acqsig;
	short frsig;
};
#define		VT_AUTO		0x00
/* End uapi/linux/vt.h */

/* Internal VT definitions */
#define VT_ALLOCATE		0x0001

#endif
