/* Minimal VGA header - only what vgacon.c needs */
#ifndef __linux_video_vga_h__
#define __linux_video_vga_h__

#include <linux/types.h>
#include <linux/io.h>
#include <asm/vga.h>

/* VGA port addresses - only those used */
#define VGA_CRT_IC 0x3D4
#define VGA_CRT_IM 0x3B4

#endif
