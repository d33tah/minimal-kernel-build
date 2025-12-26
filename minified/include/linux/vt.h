#ifndef _LINUX_VT_H
#define _LINUX_VT_H

#define MIN_NR_CONSOLES 1
#define MAX_NR_CONSOLES	63

struct vt_mode {
	char mode;
	char waitv;
	short relsig;
	short acqsig;
	short frsig;
};
#define VT_AUTO		0x00

struct vt_stat {
	unsigned short v_active;
	unsigned short v_signal;
	unsigned short v_state;
};
#define VT_GETSTATE	0x5603
#define VT_GETHIFONTMASK 0x560D

#define VT_ALLOCATE	0x0001

#endif
