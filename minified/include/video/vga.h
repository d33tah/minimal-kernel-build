/*
 * linux/include/video/vga.h -- standard VGA chipset interaction
 *
 * Copyright 1999 Jeff Garzik <jgarzik@pobox.com>
 * 
 * Copyright history from vga16fb.c:
 *	Copyright 1999 Ben Pfaff and Petr Vandrovec
 *	Based on VGA info at http://www.osdever.net/FreeVGA/home.htm 
 *	Based on VESA framebuffer (c) 1998 Gerd Knorr
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.  
 *
 */

#ifndef __linux_video_vga_h__
#define __linux_video_vga_h__

#include <asm/io.h>

#include <asm/vga.h>

/* VGA port addresses - only those used */
#define VGA_CRT_IC 0x3D4 /* CRT Controller Index - color emulation */
#define VGA_CRT_IM 0x3B4 /* CRT Controller Index - mono emulation */

#endif
