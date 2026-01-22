#ifndef _UAPI_LINUX_KD_H
#define _UAPI_LINUX_KD_H
#include <linux/types.h>
#include <linux/compiler.h>

/* KD_TEXT and KD_GRAPHICS used by VT and vgacon */
#define KD_TEXT		0x00
#define KD_GRAPHICS	0x01

/* console_font_op removed - never used */

/* console_font used by vt.c, vgacon.c */
struct console_font {
	unsigned int width, height;
	unsigned int charcount;
	unsigned char *data;
};

/* kbd_repeat, kbentry, kbsentry, kbdiacruc, kbdiacrsuc, kbkeycode -
   all removed, the ioctl functions that used them were stubbed */

#endif
