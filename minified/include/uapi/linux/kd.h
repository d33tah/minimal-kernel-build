#ifndef _UAPI_LINUX_KD_H
#define _UAPI_LINUX_KD_H
#include <linux/types.h>
#include <linux/compiler.h>

/* KD_TEXT and KD_GRAPHICS used by VT and vgacon */
#define KD_TEXT		0x00
#define KD_GRAPHICS	0x01

/* struct unipair removed - 0-ref (consolemap consumer stripped). */

#endif
