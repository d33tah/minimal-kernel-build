#ifndef _UAPI_LINUX_KD_H
#define _UAPI_LINUX_KD_H
#include <linux/types.h>
#include <linux/compiler.h>

/* Only KB_101 is used for KDGKBTYPE */
#define KB_101		0x02

/* KD_TEXT and KD_GRAPHICS used by VT and vgacon */
#define KD_TEXT		0x00
#define KD_GRAPHICS	0x01

/* KDGETMODE and KDGKBTYPE used by vt_ioctl.c */
#define KDGETMODE	0x4B3B
#define KDGKBTYPE	0x4B33

/* struct unipair removed - 0-ref (consolemap consumer stripped). */

#endif
