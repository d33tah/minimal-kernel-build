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

/* struct unipair used by consolemap.h */
struct unipair {
	unsigned short unicode;
	unsigned short fontpos;
};

/* console_font_op used by vt.c, vgacon.c, vt_kern.h, console.h */
struct console_font_op {
	unsigned int op;
	unsigned int flags;
	unsigned int width, height;
	unsigned int charcount;
	unsigned char __user *data;
};

/* console_font used by vt.c, vgacon.c */
struct console_font {
	unsigned int width, height;
	unsigned int charcount;
	unsigned char *data;
};

/* kbd_repeat, kbentry, kbsentry, kbdiacruc, kbdiacrsuc, kbkeycode -
   all removed, the ioctl functions that used them were stubbed */

#endif
